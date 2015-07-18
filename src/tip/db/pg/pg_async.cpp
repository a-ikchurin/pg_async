//============================================================================
// Name        : pg_async.cpp
// Author      : Sergei A. Fedorov
// Version     :
// Copyright   : Copyright (c) Sergei A. Fedorov 2015
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <tip/db/pg/connection.hpp>

#include <tip/db/pg/detail/basic_connection.hpp>
#include <tip/db/pg/resultset.hpp>

#include "tip/log/log.hpp"

namespace {
/** Local logging facility */
using namespace tip::log;

const std::string LOG_CATEGORY = "MAIN";
logger::event_severity DEFAULT_SEVERITY = logger::TRACE;
local
local_log(logger::event_severity s = DEFAULT_SEVERITY)
{
	return local(LOG_CATEGORY, s);
}

}  // namespace
// For more convenient changing severity, eg local_log(logger::WARNING)
using tip::log::logger;


int
main(int argc, char* argv[])
{
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <connection string> [query]\n";
		return 1;
	}
	try {
		using namespace tip::db::pg;
		logger::min_severity(logger::TRACE);
		logger::use_colors(true);

		int query_count = 0;
		boost::asio::io_service io_service;
		connection_options opts = tip::db::pg::connection_options::parse(argv[1]);
		connection_ptr conn(connection::create(io_service,
		[&] (connection_ptr c){
			local_log(logger::INFO) << "Async callback when connection ready";
			if (!query_count && argc > 2) {
				c->execute_query(argv[2],
					[](connection_lock_ptr c, resultset res, bool complete) {
					tip::log::local local = local_log(logger::INFO);
					local << "Received a result set:\n";
					for (int i = 0; i < res.columns_size(); ++i) {
						if (i > 0)
							local << "\t";
						local << "[" << res.field_name(i) << "]";
					}
					local << "\n";
				}, [&](db_error const&){
					c->terminate();
				});
				++query_count;
			} else {
				c->terminate();
			}
		},
		[] (connection_ptr c) {
			local_log(logger::INFO) << "Connection gracefully terminated";
		},
		[] (connection_ptr c, connection_error const&) {
			local_log(logger::ERROR) << "Async callback on connection error";
		},
		opts,
		{
			{"client_encoding", "UTF8"},
			{"application_name", "pg_async"}
		}));
		io_service.run();
	} catch (std::exception const& e) {
		std::cerr << "Error " << e.what() << "\n";
	} catch (...) {
		std::cerr << "Non-standard exception :(\n";
		return 1;
	}
	return 0;
}