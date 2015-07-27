/*
 * protocol_io_traits.cpp
 *
 *  Created on: Jul 19, 2015
 *      Author: zmij
 */

#include <tip/db/pg/protocol_io_traits.hpp>
#include <tip/db/pg/detail/protocol_parsers.hpp>
#include <iterator>
#include <set>

namespace tip {
namespace db {
namespace pg {

namespace traits {

namespace {

using namespace oids::type;

std::set< oid_type > BINARY_PARSERS {
	boolean, oids::type::bytea, int2, int4, int8, oid, tid, xid, cid
};
}  // namespace

void
register_parser_type(oids::type::oid_type oid)
{
	BINARY_PARSERS.insert(oid);
}

bool
has_binary_parser(oids::type::oid_type oid)
{
	return BINARY_PARSERS.count(oid);
}

}  // namespace traits

bool
protocol_parser< std::string, TEXT_DATA_FORMAT >::operator ()(std::istream& in)
{
	in.unsetf(std::ios::skipws);
	std::istream_iterator<char> i(in);
	std::istream_iterator<char> e;
	if (i == e) {
		in.setstate(std::ios_base::failbit);
		return false;
	}
	std::string tmp(i, e);
	tmp.swap(value);
	return true;
}

bool
protocol_parser< std::string, TEXT_DATA_FORMAT >::operator ()(buffer_type& buffer)
{
	if (buffer.empty())
		return false;
	std::string tmp(buffer.begin(), buffer.end());
	tmp.swap(value);
	return true;
}

namespace {

const std::set< std::string > TRUE_LITERALS {
	"TRUE",
	"t",
	"true",
	"y",
	"yes",
	"on",
	"1"
};

const std::set< std::string > FALSE_LITERALS {
	"FALSE",
	"f",
	"false",
	"n",
	"no",
	"off",
	"0"
};

}  // namespace

bool
protocol_parser< bool, TEXT_DATA_FORMAT >::operator ()(std::istream& in)
{
	std::string literal;
	if (protocol_read< TEXT_DATA_FORMAT >(literal)(in)) {
		if (TRUE_LITERALS.count(literal)) {
			value = true;
			return true;
		} else if (FALSE_LITERALS.count(literal)) {
			value = false;
			return true;
		}
		in.setstate(std::ios_base::failbit);
	}
	return false;
}

bool
protocol_parser< bool, TEXT_DATA_FORMAT >::operator ()(buffer_type& buffer)
{
	std::string literal;
	if (protocol_read<TEXT_DATA_FORMAT>(literal)(buffer)) {
		if (TRUE_LITERALS.count(literal)) {
			value = true;
			return true;
		} else if (FALSE_LITERALS.count(literal)) {
			value = false;
			return true;
		}
	}
	return false;
}

bool
protocol_parser< bytea, TEXT_DATA_FORMAT >::operator()(std::istream& in)
{
	std::vector<byte> data;
	std::istream_iterator<char> b(in);
	std::istream_iterator<char> e;

	auto result = detail::bytea_parser().parse(b, e, std::back_inserter(data));
	if (result.first) {
		value.data.swap(data);
		return true;
	}
	return false;
}

bool
protocol_parser< bytea, TEXT_DATA_FORMAT >::operator()(buffer_type& buffer)
{
	std::vector<byte> data;
	auto result = detail::bytea_parser().parse(buffer.begin(), buffer.end(),
			std::back_inserter(data));
	if (result.first) {
		value.data.swap(data);
		return true;
	}
	return false;
}

}  // namespace pg
}  // namespace db
}  // namespace tip