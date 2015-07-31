/*
 * connection_fsm.hpp
 *
 *  Created on: Jul 30, 2015
 *      Author: zmij
 */

#ifndef LIB_PG_ASYNC_SRC_TIP_DB_PG_DETAIL_BASIC_CONNECTION_NEW_HPP_
#define LIB_PG_ASYNC_SRC_TIP_DB_PG_DETAIL_BASIC_CONNECTION_NEW_HPP_

#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <map>
#include <stack>
#include <set>
#include <memory>

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/euml/operator.hpp>

#include <tip/db/pg/common.hpp>
#include <tip/db/pg/error.hpp>

#include <tip/db/pg/detail/protocol.hpp>
#include <tip/db/pg/detail/md5.hpp>
#include <tip/db/pg/detail/result_impl.hpp>

#include <tip/db/pg/log.hpp>

namespace tip {
namespace db {
namespace pg {

class resultset;

namespace detail {

namespace {
/** Local logging facility */
using namespace tip::log;

const std::string LOG_CATEGORY = "PGFSM";
logger::event_severity DEFAULT_SEVERITY = logger::TRACE;
local
local_log(logger::event_severity s = DEFAULT_SEVERITY)
{
	return local(LOG_CATEGORY, s);
}

}  // namespace
// For more convenient changing severity, eg local_log(logger::WARNING)
using tip::log::logger;

struct transport_connected {};

struct authn_event {
	auth_states state;
	message_ptr message;
};

struct complete {};

struct begin {};
struct commit {};
struct rollback {};

struct execute {
	std::string expression;
};
struct execute_prepared {
	std::string expression;
	std::vector< oids::type::oid_type > param_types;
	std::vector< byte > params;
};

struct ready_for_query {
	char status;
};

struct row_description {
	mutable std::vector<field_description> fields;
};

struct no_data {}; // Prepared query doesn't return data

struct terminate {};

template < typename TransportType >
struct connection_fsm_ :
		public boost::msm::front::state_machine_def< connection_fsm_< TransportType > >,
		public std::enable_shared_from_this< connection_fsm_< TransportType > > {

	//@{
	/** @name Typedefs for MSM types */
	template < typename ... T >
	using Row = boost::msm::front::Row< T ... >;
	template < typename ... T >
	using Internal = boost::msm::front::Internal< T ... >;
	typedef boost::msm::front::none none;
	template < typename T >
	using Not = boost::msm::front::euml::Not_< T >;
	//@}
	//@{
	/** @name Misc typedefs */
	typedef TransportType transport_type;
	typedef boost::msm::back::state_machine< connection_fsm_< transport_type > > connection;
	typedef std::shared_ptr< message > message_ptr;
	typedef std::map< std::string, row_description > prepared_statements_map;
	typedef std::shared_ptr< result_impl > result_ptr;
	//@}
	//@{
	/** @name Actions */
	struct on_connection_error {
		template < typename SourceState, typename TargetState >
		void
		operator() (connection_error const& err, connection& fsm, SourceState&, TargetState&)
		{
			local_log(logger::ERROR) << "connection::on_connection_error Error: "
					<< err.what();
		}
	};
	struct start_authn {
		template < typename FSM, typename SourceState, typename TargetState >
		void
		operator() (complete const&, FSM&, SourceState&, TargetState&)
		{
			local_log() << "connection::start_authn";
		}
	};
	struct start_idle {
		template < typename FSM, typename SourceState, typename TargetState >
		void
		operator() (complete const&, FSM&, SourceState&, TargetState&)
		{
			local_log() << "connection::start_idle";
		}
	};
	struct start_transaction {
		template < typename FSM, typename SourceState, typename TargetState >
		void
		operator() (begin const&, FSM&, SourceState&, TargetState&)
		{
			local_log() << "connection::start_transaction";
		}
	};
	struct finish_transaction {
		template < typename SourceState, typename TargetState >
		void
		operator() (ready_for_query const&, connection&, SourceState&, TargetState&)
		{
			local_log() << "connection::finish_transaction";
		}
		template < typename SourceState, typename TargetState >
		void
		operator() (command_complete const&, connection&, SourceState&, TargetState&)
		{
			local_log() << "connection::finish_transaction";
		}
	};
	struct on_command_complete {
		template < typename FSM, typename SourceState, typename TargetState >
		void
		operator() (command_complete const& evt, FSM&, SourceState&, TargetState&)
		{
			local_log() << "Command complete " << evt.command_tag;
		}
	};
	struct disconnect {
		template < typename SourceState, typename TargetState >
		void
		operator() (terminate const&, connection& fsm, SourceState&, TargetState&)
		{
			local_log() << "connection: disconnect";
			fsm.send(message(terminate_tag));
		}
	};
	//@}
	//@{
	/** @name States */
	struct unplugged : public boost::msm::front::state<> {
		typedef boost::mpl::vector<
				terminate,
				begin,
				commit,
				rollback,
				execute,
				execute_prepared
			> deferred_events;
	};

	struct terminated : boost::msm::front::terminate_state<> {
		template < typename Event >
		void
		on_entry(Event const&, connection& fsm)
		{
			local_log(logger::DEBUG) << "entering: terminated";
			fsm.send(message(terminate_tag));
		}
	};

	struct t_conn : public boost::msm::front::state<> {
		typedef boost::mpl::vector<
				terminate,
				begin,
				commit,
				rollback,
				execute,
				execute_prepared
			> deferred_events;
		void
		on_entry(connection_options const& opts, connection& fsm)
		{
			local_log() << "entering: transport connecting";
			fsm.connect_transport(opts);
		}

		template < typename Event, typename FSM >
		void
		on_entry(Event const&, FSM&)
		{ local_log() << "entering: transport connecting - unknown variant"; }

		template < typename Event >
		void
		on_exit(Event const&, connection& fsm)
		{
			local_log() << "leaving:  transport connecting";
			fsm.start_read();
		}
	};

	struct authn : public boost::msm::front::state<> {
		typedef boost::mpl::vector<
				terminate,
				begin,
				commit,
				rollback,
				execute,
				execute_prepared
			> deferred_events;
		template < typename Event >
		void
		on_entry(Event const&, connection& fsm)
		{
			local_log() << "entering: authenticating";
			fsm.send_startup_message();
		}
		template < typename Event, typename FSM >
		void
		on_exit(Event const&, FSM&)
		{ local_log() << "leaving: authenticating"; }

		struct handle_authn_event {
			template < typename SourceState, typename TargetState >
			void
			operator() (authn_event const& evt, connection& fsm, SourceState&, TargetState&)
			{
				local_log() << "authn: handle auth_event";
				switch (evt.state) {
					case OK: {
						local_log() << "Authenticated with postgre server";
						break;
					}
					case Cleartext : {
						local_log() << "Cleartext password requested";
						message pm(password_message_tag);
						pm.write(fsm.options().password);
						fsm.send(pm);
						break;
					}
					case MD5Password: {
						#ifdef WITH_TIP_LOG
						local_log() << "MD5 password requested";
						#endif
						// Read salt
						std::string salt;
						evt.message->read(salt, 4);
						connection_options const& co = fsm.options();
						// Calculate hash
						std::string pwdhash = boost::md5((co.password + co.user).c_str()).digest().hex_str_value();
						std::string md5digest = std::string("md5") + boost::md5( (pwdhash + salt).c_str() ).digest().hex_str_value();
						// Construct and send message
						message pm(password_message_tag);
						pm.write(md5digest);
						fsm.send(pm);
						break;
					}
					default : {
						std::stringstream err;
						err << "Unsupported authentication scheme "
								<< evt.state << " requested by server";
						fsm.process_event(connection_error(err.str()));
					}
				}
			}
		};

		struct internal_transition_table : boost::mpl::vector<
			Internal< authn_event, handle_authn_event,	none >
		> {};
	};

	struct idle : public boost::msm::front::state<> {
		template < typename Event, typename FSM >
		void
		on_entry(Event const&, FSM&)
		{ local_log() << "entering: idle"; }
		template < typename Event, typename FSM >
		void
		on_exit(Event const&, FSM&)
		{ local_log() << "leaving: idle"; }

		struct internal_transition_table : boost::mpl::vector<
		/*				Event			Action		Guard	 */
		/*			+-----------------+-----------+---------+*/
			Internal< ready_for_query,	none,		none 	>
		> {};
	};

	struct transaction_ : public boost::msm::front::state_machine_def<transaction_> {
		//@{
		/** @name Transaction entry-exit */
		template < typename Event >
		void
		on_entry(Event const&, connection& fsm)
		{
			local_log(logger::DEBUG) << "entering: transaction";
			connection_ = &fsm;
		}
		template < typename Event, typename FSM >
		void
		on_exit(Event const&, FSM&)
		{ local_log(logger::DEBUG) << "leaving: transaction"; }
		//@}

		typedef boost::mpl::vector< terminate > deferred_events;
		typedef boost::msm::back::state_machine< transaction_ > tran_fsm;

		//@{
		/** State forwards */
		struct tran_error;
		//@}
		//@{
		/** @name Actions */
		struct  commit_transaction {
			template < typename SourceState, typename TargetState >
			void
			operator() (commit const&, tran_fsm& tran, SourceState&, TargetState&)
			{
				local_log() << "transaction::commit_transaction";
				tran.connection_->send_commit();
			}
		};
		struct  rollback_transaction {
			template < typename SourceState, typename TargetState >
			void
			operator() (rollback const&, tran_fsm& tran, SourceState&, TargetState&)
			{
				local_log() << "transaction::rollback_transaction";
				tran.connection_->send_rollback();
			}

			template < typename SourceState, typename TargetState >
			void
			operator() (query_error const&, tran_fsm& tran, SourceState&, TargetState&)
			{
				local_log() << "transaction::rollback_transaction (on error)";
				tran.connection_->send_rollback();
			}
			template < typename Event, typename TargetState >
			void
			operator() (Event const&, tran_fsm& tran, tran_error&, TargetState&)
			{
				local_log() << "transaction::rollback_transaction (on error)";
				tran.connection_->send_rollback();
			}
		};
		//@}
		//@{
		/** @name Transaction sub-states */
		struct starting : public boost::msm::front::state<> {
			typedef boost::mpl::vector<
					execute,
					execute_prepared,
					commit,
					rollback
				> deferred_events;

			//template < typename Event >
			void
			on_entry(begin const& evt, tran_fsm& tran)
			{
				local_log() << "entering: starting";
				tran.connection_->send_begin();
			}
			template < typename Event, typename FSM >
			void
			on_exit(Event const&, FSM&)
			{ local_log() << "leaving: starting"; }
			struct internal_transition_table : boost::mpl::vector<
			/*				Event				Action		Guard	 */
			/*			+---------------------+-----------+---------+*/
				Internal< command_complete,		none,		none 	>
			> {};
		};

		struct idle : public boost::msm::front::state<> {
			template < typename Event, typename FSM >
			void
			on_entry(Event const&, FSM&)
			{ local_log() << "entering: idle (transaction)"; }
			template < typename Event, typename FSM >
			void
			on_exit(Event const&, FSM&)
			{ local_log() << "leaving: idle (transaction)"; }

			struct internal_transition_table : boost::mpl::vector<
			/*				Event				Action					Guard	 */
			/*			+---------------------+-----------------------+---------+*/
				Internal< command_complete,		on_command_complete,	none 	>,
				Internal< ready_for_query,		none,					none 	>
			> {};
		};

		struct tran_error : public boost::msm::front::state<> {
			template < typename Event, typename FSM >
			void
			on_entry(Event const&, FSM&)
			{ local_log() << "entering: tran_error"; }
			template < typename Event, typename FSM >
			void
			on_exit(Event const&, FSM&)
			{ local_log() << "leaving: tran_error"; }
		};
		struct exiting : public boost::msm::front::state<> {
			template < typename Event, typename FSM >
			void
			on_entry(Event const&, FSM&)
			{ local_log() << "entering: exit transaction"; }
			template < typename Event, typename FSM >
			void
			on_exit(Event const&, FSM&)
			{ local_log() << "leaving: exit transaction"; }
			struct internal_transition_table : boost::mpl::vector<
			/*				Event				Action					Guard	 */
			/*			+---------------------+-----------------------+---------+*/
				Internal< command_complete,		none,					none 	>
			> {};
		};

		struct simple_query_ : public boost::msm::front::state_machine_def<simple_query_> {
			template < typename Event >
			void
			on_entry(Event const& q, tran_fsm& tran)
			{
				local_log(logger::WARNING)
						<< tip::util::MAGENTA << "entering: simple query (unexpected event)";
				connection_ = tran.connection_;
			}
			void
			on_entry(execute const& q, tran_fsm& tran)
			{
				local_log(logger::DEBUG) << tip::util::MAGENTA << "entering: simple query (execute event)";
				connection_ = tran.connection_;
				message m(query_tag);
				m.write(q.expression);
				connection_->send(m);
			}
			template < typename Event, typename FSM >
			void
			on_exit(Event const&, FSM&)
			{ local_log(logger::DEBUG) << tip::util::MAGENTA << "leaving: simple query"; }

			typedef boost::mpl::vector< execute, execute_prepared, commit, rollback > deferred_events;

			//@{
			/** @name Simple query sub-states */
			struct waiting : public boost::msm::front::state<> {
				template < typename Event, typename FSM >
				void
				on_entry(Event const&, FSM&)
				{ local_log() << "entering: waiting"; }
				template < typename Event, typename FSM >
				void
				on_exit(Event const&, FSM&)
				{ local_log() << "leaving: waiting"; }
				// TODO Internal transition for command complete - non select query
				struct non_select_result {
					template < typename FSM >
					void
					operator()(command_complete const& evt, FSM&, waiting&, waiting&)
					{
						local_log() << "Non-select query complete "
								<< evt.command_tag;
					}
				};

				struct internal_transition_table : boost::mpl::vector<
					Internal< command_complete,	non_select_result,	none >
				> {};
			};

			struct fetch_data : public boost::msm::front::state<> {
				typedef boost::mpl::vector< ready_for_query > deferred_events;

				fetch_data() : result_( new result_impl ) {}

				template < typename Event, typename FSM >
				void
				on_entry(Event const&, FSM&)
				{ local_log() << "entering: fetch_data (unexpected event)"; }

				template < typename FSM >
				void
				on_entry(row_description const& rd, FSM&)
				{
					local_log() << "entering: fetch_data column count: "
							<< rd.fields.size();
					result_.reset(new result_impl);
					result_->row_description().swap(rd.fields);
				}
				template < typename Event, typename FSM >
				void
				on_exit(Event const&, FSM&)
				{
					local_log() << "leaving: fetch_data result size: "
							<< result_->size();
				}

				struct parse_data_row {
					template < typename FSM, typename TargetState >
					void
					operator() (row_data const& row, FSM&, fetch_data& fetch, TargetState&)
					{
						fetch.result_->rows().push_back(row);
					}
				};

				struct internal_transition_table : boost::mpl::vector<
					Internal< row_data,	parse_data_row,	none >
				> {};

				result_ptr result_;
			};
			typedef waiting initial_state;
			//@}
			//@{
			/** @name Transitions */
			struct transition_table : boost::mpl::vector<
				/*		Start			Event				Next			Action			Guard			  */
				/*  +-----------------+-------------------+---------------+---------------+-----------------+ */
				 Row<	waiting,		row_description,	fetch_data,		none,			none			>,
				 Row<	fetch_data,		command_complete,	waiting,		none,			none			>
			> {};
			connection* connection_;
		};
		typedef boost::msm::back::state_machine< simple_query_ > simple_query;

		struct extended_query_ : public boost::msm::front::state_machine_def<extended_query_> {

			typedef boost::msm::back::state_machine< extended_query_ > extended_query;

			extended_query_() : connection_(nullptr), row_limit_(0),
					result_(new result_impl) {}

			template < typename Event, typename FSM >
			void
			on_entry(Event const&, FSM&)
			{ local_log(logger::WARNING) << tip::util::MAGENTA << "entering: extended query (unexpected event)"; }
			void
			on_entry(execute_prepared const& q, tran_fsm& tran)
			{
				local_log(logger::DEBUG) << tip::util::MAGENTA << "entering: extended query";
				connection_ = tran.connection_;
				query_ = q;
				std::ostringstream os;
				os << query_.expression;
				if (!query_.param_types.empty()) {
					os << "{";
					std::ostream_iterator< oids::type::oid_type > out(os, ",");
					std::copy( query_.param_types.begin(), query_.param_types.end() - 1, out );
					os << query_.param_types.back() << "}";
				}
				local_log() << "query signature " << os.str();
				query_name_ = "q_" +
					std::string( boost::md5( os.str().c_str() ).digest().hex_str_value() );
				//result_.reset(new result_impl);
			}
			template < typename Event, typename FSM >
			void
			on_exit(Event const&, FSM&)
			{ local_log(logger::DEBUG) << tip::util::MAGENTA << "leaving: extended query"; }

			typedef boost::mpl::vector< execute, execute_prepared, commit, rollback > deferred_events;

			//@{
			struct store_prepared_desc {
				template < typename SourceState, typename TargetState >
				void
				operator() (row_description const& row, extended_query& fsm,
						SourceState&, TargetState&)
				{
					fsm.result_.reset(new result_impl);
					fsm.result_->row_description() = row.fields; // copy!
					fsm.connection_->set_prepared(fsm.query_name_, row);
				}
				template < typename SourceState, typename TargetState >
				void
				operator() (no_data const&, extended_query& fsm,
						SourceState&, TargetState&)
				{
					fsm.result_.reset(new result_impl);
					row_description row;
					fsm.connection_->set_prepared(fsm.query_name_, row);
				}
			};
			struct parse_data_row {
				template < typename SourceState, typename TargetState >
				void
				operator() (row_data const& row, extended_query& fsm,
						SourceState&, TargetState&)
				{
					fsm.result_->rows().push_back(row);
				}
			};
			struct complete_execution {
				template < typename SourceState, typename TargetState >
				void
				operator() (command_complete const& complete, extended_query& fsm,
						SourceState&, TargetState&)
				{
					local_log() << "Execute complete " << complete.command_tag
							<< " resultset columns "
							<< fsm.result_->row_description().size()
							<< " rows " << fsm.result_->size();
				}
			};
			//@}
			//@{
			/** @name Extended query sub-states */
			struct prepare : public boost::msm::front::state<> {};

			struct parse : public boost::msm::front::state<> {
				template < typename Event, typename FSM >
				void
				on_entry(Event const&, FSM&)
				{ local_log() << "entering: parse (unexpected fsm)"; }
				template < typename Event >
				void
				on_entry(Event const&, extended_query& fsm)
				{
					local_log() << "entering: parse";
					local_log(logger::DEBUG) << "Parse query " << fsm.query_.expression;
					message parse(parse_tag);
					parse.write(fsm.query_name_);
					parse.write(fsm.query_.expression);
					parse.write( (smallint)fsm.query_.param_types.size() );
					for (oids::type::oid_type oid : fsm.query_.param_types) {
						parse.write( (integer)oid );
					}

					message describe(describe_tag);
					describe.write('S');
					describe.write(fsm.query_name_);
					parse.pack(describe);

					parse.pack(message(sync_tag));

					fsm.connection_->send(parse);
				}
				template < typename Event, typename FSM >
				void
				on_exit(Event const&, FSM&)
				{ local_log() << "leaving: parse"; }

				struct internal_transition_table : boost::mpl::vector<
					Internal< row_description,	store_prepared_desc,	none >,
					Internal< no_data,			store_prepared_desc,	none >
				> {};
			};

			struct bind : public boost::msm::front::state<> {
				template < typename Event, typename FSM >
				void
				on_entry(Event const&, FSM&)
				{ local_log() << "entering: bind (unexpected fsm)"; }
				template < typename Event >
				void
				on_entry( Event const&, extended_query& fsm )
				{
					local_log() << "entering: bind";
					message bind(bind_tag);
					bind.write(fsm.portal_name_);
					bind.write(fsm.query_name_);
					if (!fsm.query_.params.empty()) {
						auto out = bind.output();
						std::copy(fsm.query_.params.begin(), fsm.query_.params.end(), out);
					} else {
						bind.write((smallint)0); // parameter format codes
						bind.write((smallint)0); // number of parameters
					}
					if (fsm.connection_->is_prepared(fsm.query_name_)) {
						row_description const& row =
								fsm.connection_->get_prepared(fsm.query_name_);
						bind.write((smallint)row.fields.size());
						for (auto fd : row.fields) {
							bind.write((smallint)fd.format_code);
						}
					} else {
						bind.write((smallint)0); // no row description
					}

					bind.pack(message(sync_tag));

					fsm.connection_->send(bind);
				}
				template < typename Event, typename FSM >
				void
				on_exit(Event const&, FSM&)
				{ local_log() << "leaving: bind"; }
			};

			struct exec : public boost::msm::front::state<> {
				template < typename Event, typename FSM >
				void
				on_entry(Event const&, FSM&)
				{ local_log() << "entering: execute (unexpected fsm)"; }
				template < typename Event >
				void
				on_entry( Event const&, extended_query& fsm )
				{
					local_log() << "entering: execute";
					message execute(execute_tag);
					execute.write(fsm.portal_name_);
					execute.write(fsm.row_limit_);
					execute.pack(message(sync_tag));
					fsm.connection_->send(execute);
				}
				template < typename Event, typename FSM >
				void
				on_exit(Event const&, FSM&)
				{ local_log() << "leaving: execute"; }

				struct internal_transition_table : boost::mpl::vector<
					Internal< row_data,			parse_data_row,			none >,
					Internal< command_complete, complete_execution, 	none >
				> {};
			};

			typedef prepare initial_state;
			//@}

	        struct is_prepared
	        {
	            template < class EVT, class SourceState, class TargetState>
	            bool
				operator()(EVT const& evt, extended_query& fsm,
						SourceState& src,TargetState& tgt)
	            {
	            	if (fsm.connection_) {
	            		return fsm.connection_->is_prepared(fsm.query_name_);
	            	}
	                return false;
	            }
	        };
			//@{
			/** Transitions for extended query  */
			struct transition_table : boost::mpl::vector<
				/*		Start			Event				Next			Action			Guard			      */
				/*  +-----------------+-------------------+---------------+---------------+---------------------+ */
				 Row<	prepare,		none,				parse,			none,			Not<is_prepared>	>,
				 Row<	prepare,		none,				bind,			none,			is_prepared			>,
				 Row<	parse,			ready_for_query,	bind,			none,			none				>,
				 Row<	bind,			ready_for_query,	exec,			none,			none				>
			>{};
			//@}
			connection* connection_;

			execute_prepared query_;
			std::string query_name_;
			std::string portal_name_;
			integer row_limit_;

			result_ptr result_;
		};
		typedef boost::msm::back::state_machine< extended_query_ > extended_query;

		typedef starting initial_state;
		//@}


		//@{
		/** @name Transition table for transaction */
		struct transition_table : boost::mpl::vector<
			/*		Start			Event				Next			Action						Guard				  */
			/*  +-----------------+-------------------+-----------+---------------------------+---------------------+ */
			 Row<	starting,		ready_for_query,	idle,			none,						none			>,
			/*  +-----------------+-------------------+-----------+---------------------------+---------------------+ */
			 Row<	idle,			commit,				exiting,		commit_transaction,			none			>,
			 Row<	idle,			rollback,			exiting,		rollback_transaction,		none			>,
			/*  +-----------------+-------------------+-----------+---------------------------+---------------------+ */
			 Row<	idle,			execute,			simple_query,	none,						none			>,
			 Row<	simple_query,	ready_for_query,	idle,			none,						none			>,
			 Row<	simple_query,	query_error,		tran_error,		none,						none			>,
			/*  +-----------------+-------------------+-----------+---------------------------+---------------------+ */
			 Row<	idle,			execute_prepared,	extended_query,	none,						none			>,
			 Row<	extended_query,	ready_for_query,	idle,			none,						none			>,
		 	 Row<	extended_query,	query_error,		tran_error,		none,						none			>,
			/*  +-----------------+-------------------+-----------+---------------------------+---------------------+ */
			 Row< 	tran_error,		ready_for_query,	exiting,		rollback_transaction,		none			>
		> {};

		template < typename Event, typename FSM >
		void
		no_transition(Event const& e, FSM&, int state)
		{
			local_log(logger::ERROR) << "No transition from state " << state
					<< " on event " << typeid(e).name() << " (in transaction)";
		}
		//@}

		connection* connection_;
	}; // transaction state machine
	typedef boost::msm::back::state_machine< transaction_ > transaction;

	typedef unplugged initial_state;
	//@}
	//@{
	/** @name Transition table */
	struct transition_table : boost::mpl::vector<
		/*		Start			Event				Next			Action						Guard					  */
		/*  +-----------------+-------------------+---------------+---------------------------+---------------------+ */
	    Row <	unplugged,		connection_options,	t_conn,			none,						none				>,
	    Row <	t_conn,			complete,			authn,			none,						none				>,
	    Row <	t_conn,			connection_error,	terminated,		on_connection_error,		none				>,
	    Row <	authn,			ready_for_query,	idle,			none,						none				>,
	    Row <	authn,			connection_error,	terminated,		on_connection_error,		none				>,
	    /*									Transitions from idle													  */
		/*  +-----------------+-------------------+---------------+---------------------------+---------------------+ */
	    Row	<	idle,			begin,				transaction,	start_transaction,			none				>,
	    Row <	idle,			terminate,			terminated,		disconnect,					none				>,
	    Row <	idle,			connection_error,	terminated,		on_connection_error,		none				>,
		/*  +-----------------+-------------------+---------------+---------------------------+---------------------+ */
	    Row <	transaction,	ready_for_query,	idle,			finish_transaction,			none				>,
	    Row <	transaction,	connection_error,	terminated,		on_connection_error,		none				>
	> {};
	//@}
	template < typename Event, typename FSM >
	void
	no_transition(Event const& e, FSM&, int state)
	{
		local_log(logger::ERROR) << "No transition from state " << state
				<< " on event " << typeid(e).name();
	}

	//@{
	typedef boost::asio::io_service io_service;
	typedef std::map< std::string, std::string > client_options_type;
	typedef connection_fsm_< transport_type > this_type;
	typedef std::enable_shared_from_this< this_type > shared_base;

	typedef std::function< void (boost::system::error_code const& error,
			size_t bytes_transferred) > asio_io_handler;
	//@}

	//@{
	connection_fsm_(io_service& svc, client_options_type const& co)
		: transport_(svc), client_opts_(co), strand_(svc),
		  serverPid_(0), serverSecret_(0)
	{
		incoming_.prepare(8192); // FIXME Magic number, move to configuration
	}
	//@}

	void
	connect_transport(connection_options const& opts)
	{
		if (opts.uri.empty()) {
			throw connection_error("No connection uri!");
		}
		if (opts.database.empty()) {
			throw connection_error("No database!");
		}
		if (opts.user.empty()) {
			throw connection_error("User not specified!");
		}
		conn_opts_ = opts;
		transport_.connect_async(conn_opts_,
            boost::bind(&connection_fsm_::handle_connect,
                shared_base::shared_from_this(), boost::asio::placeholders::error));
	}
	void
	start_read()
	{
		transport_.async_read(incoming_,
			strand_.wrap( boost::bind(
				&connection_fsm_::handle_read, shared_base::shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred )
		));
	}

	void
	send_startup_message()
	{
		message m(empty_tag);
		create_startup_message(m);
		send(m);
	}
	void
	send_begin()
	{
		message m(query_tag);
		m.write("begin");
		send(m);
	}
	void
	send_commit()
	{
		message m(query_tag);
		m.write("commit");
		send(m);
	}
	void
	send_rollback()
	{
		message m(query_tag);
		m.write("rollback");
		send(m);
	}
	void
	send(message const& m, asio_io_handler handler = asio_io_handler())
	{
		if (transport_.connected()) {
			auto data_range = m.buffer();
			if (!handler) {
				handler = boost::bind(
						&this_type::handle_write, shared_base::shared_from_this(),
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred );
			}
			transport_.async_write(
				boost::asio::buffer(&*data_range.first,
						data_range.second - data_range.first),
				strand_.wrap(handler)
			);
		}
	}

	connection_options const&
	options() const
	{ return conn_opts_; }

	bool
	is_prepared ( std::string const& query )
	{
		return prepared_.count(query);
	}

	void
	set_prepared( std::string const& query, row_description const& row )
	{
		prepared_.insert(std::make_pair(query, row));
	}
	row_description const&
	get_prepared( std::string const& query_name ) const
	{
		// FIXME Handle "not there"
		auto f = prepared_.find(query_name);
		if (f != prepared_.end()) {
			return f->second;
		}
		throw db_error("Query is not prepared");
	}
private:
	connection&
	fsm()
	{
		return static_cast< connection& >(*this);
	}
    void
    handle_connect(boost::system::error_code const& ec)
    {
        if (!ec) {
            fsm().process_event(complete());
        } else {
            fsm().process_event( connection_error(ec.message()) );
        }
    }
	void
	handle_read(boost::system::error_code const& ec, size_t bytes_transferred)
	{
		if (!ec) {
			// read message
			std::istreambuf_iterator<char> in(&incoming_);
			read_message(in, bytes_transferred);
			// start async operation again
			start_read();
		} else {
			// Socket error - force termination
			fsm().process_event(connection_error(ec.message()));
		}
	}
	void
	handle_write(boost::system::error_code const& ec, size_t bytes_transferred)
	{
		if (ec) {
			// Socket error - force termination
			fsm().process_event(connection_error(ec.message()));
		}
	}

	template < typename InputIter, typename OutputIter >
	InputIter
	copy(InputIter in, InputIter end, size_t max, OutputIter out)
	{
		for (int i = 0; i < max && in != end; ++i) {
			*out++ = *in++;
		}
		return in;
	}

	void
	read_message( std::istreambuf_iterator< char > in, size_t max_bytes )
	{
		const size_t header_size = sizeof(integer) + sizeof(byte);
	    while (max_bytes > 0) {
	        size_t loop_beg = max_bytes;
	        if (!message_) {
	            message_.reset(new detail::message);
	        }
	        auto out = message_->output();

	        std::istreambuf_iterator<char> eos;
	        if (message_->buffer_size() < header_size) {
	            // Read the header
	            size_t to_read = std::min((header_size - message_->buffer_size()), max_bytes);
	            in = copy(in, eos, to_read, out);
	            max_bytes -= to_read;
	        }
	        if (message_->length() > message_->size()) {
	            // Read the message body
	            size_t to_read = std::min(message_->length() - message_->size(), max_bytes);
	            in = copy(in, eos, to_read, out);
	            max_bytes -= to_read;
	        	assert(message_->size() <= message_->length()
	        			&& "Read too much from the buffer" );
	        }
	        if (message_->size() >= 4 && message_->length() == message_->size()) {
	            message_ptr m = message_;
	            m->reset_read();
	            handle_message(m);
	            message_.reset();
	        }
	        {
	            local_log(logger::OFF) << loop_beg - max_bytes
	            		<< " bytes consumed, " << max_bytes << " bytes left";
	        }
	    }
	}

	void
	create_startup_message(message& m)
	{
		m.write(PROTOCOL_VERSION);
		// Create startup packet
		m.write(options::USER);
		m.write(conn_opts_.user);
		m.write(options::DATABASE);
		m.write(conn_opts_.database);

		for (auto opt : client_opts_) {
			m.write(opt.first);
			m.write(opt.second);
		}
		// trailing terminator
		m.write('\0');
	}

	void
	handle_message(message_ptr m)
	{
		message_tag tag = m->tag();
	    if (message::backend_tags().count(tag)) {
	    	switch (tag) {
	    		case authentication_tag: {
	    			integer auth_state(-1);
	    			m->read(auth_state);
	    			fsm().process_event(authn_event{ (auth_states)auth_state, m });
	    			break;
	    		}
				case command_complete_tag: {
					command_complete cmpl;
					m->read(cmpl.command_tag);
					local_log() << "Command complete ("
							<< cmpl.command_tag << ")";
					fsm().process_event(cmpl);
					break;
				}
				case backend_key_data_tag: {
					m->read(serverPid_);
					m->read(serverSecret_);
					break;
				}
				case error_response_tag: {
					notice_message msg;
					m->read(msg);

					local_log(logger::ERROR) << "Error " << msg ;
					query_error err(msg.message, msg.severity,
							msg.sqlstate, msg.detail);
					fsm().process_event(err);
					break;
				}
				case parameter_status_tag: {
					std::string key;
					std::string value;

					m->read(key);
					m->read(value);

					local_log() << "Parameter " << key << " = " << value;
					client_opts_[key] = value;
					break;
				}
				case notice_response_tag : {
					notice_message msg;
					m->read(msg);
					local_log(logger::INFO) << "Notice " << msg;
					break;
				}
				case ready_for_query_tag: {
					char stat(0);
					m->read(stat);
					local_log() << "Database "
						<< (util::CLEAR) << (util::RED | util::BRIGHT)
						<< conn_opts_.uri
						<< "[" << conn_opts_.database << "]"
						<< logger::severity_color()
						<< " is ready for query (" << stat << ")";
					fsm().process_event(ready_for_query{ stat });
					break;
				}
				case row_description_tag: {
					row_description rd;
					smallint col_cnt;
					m->read(col_cnt);
					rd.fields.reserve(col_cnt);
					for (int i =0; i < col_cnt; ++i) {
						field_description fd;
						if (m->read(fd)) {
							rd.fields.push_back(fd);
						} else {
							local_log(logger::ERROR)
									<< "Failed to read field description " << i;
							// FIXME Process error
						}
					}
					fsm().process_event(rd);
					break;
				}
				case data_row_tag: {
					row_data row;
					if (m->read(row)) {
						fsm().process_event(row);
					} else {
						// FIXME Process error
						local_log(logger::ERROR) << "Failed to read data row";
					}
					break;
				}
				case parse_complete_tag: {
					local_log() << "Parse complete";
					break;
				}
				case parameter_desription_tag: {
					local_log() << "Parameter descriptions";
					break;
				}
				case bind_complete_tag: {
					local_log() << "Bind complete";
					break;
				}
				case no_data_tag: {
					fsm().process_event(no_data());
					break;
				}
				default: {
				    {
				        local_log(logger::TRACE) << "Unhandled message "
				        		<< (util::MAGENTA | util::BRIGHT)
				        		<< (char)tag
				        		<< logger::severity_color();
				    }
					break;
				}
			}

	    }
	}
private:
	transport_type transport_;

	connection_options conn_opts_;
	client_options_type client_opts_;

	boost::asio::io_service::strand strand_;
	boost::asio::streambuf incoming_;

	message_ptr message_;

	integer serverPid_;
	integer	serverSecret_;

	prepared_statements_map prepared_;
};

class basic_connection;
typedef std::shared_ptr< basic_connection > basic_connection_ptr;

class basic_connection : public boost::noncopyable {
public:
	typedef boost::asio::io_service io_service;
	typedef std::map< std::string, std::string > client_options_type;
public:
	static basic_connection_ptr
	create(io_service& svc, connection_options const&, client_options_type const&);
	virtual ~basic_connection() {}

protected:
	basic_connection();
private:
};


template < typename TransportType >
class concrete_connection : public basic_connection,
	public boost::msm::back::state_machine< connection_fsm_< TransportType > >  {
public:
	typedef TransportType transport_type;
	typedef boost::msm::back::state_machine< connection_fsm_< transport_type > > state_machine_type;
public:
	concrete_connection(io_service& svc,
			client_options_type const& co)
		: basic_connection(), state_machine_type(std::ref(svc), co)
	{
	}
};

}  // namespace detail
}  // namespace pg
}  // namespace db
}  // namespace tip


#endif /* LIB_PG_ASYNC_SRC_TIP_DB_PG_DETAIL_BASIC_CONNECTION_NEW_HPP_ */