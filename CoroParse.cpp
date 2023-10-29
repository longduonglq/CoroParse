// CoroParse.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "CoroParse.hpp"

namespace ex
{
	using namespace tao::pegtl;
	using namespace coroparse;

	struct QuotedString : seq<
		one< '\"', '\'' >,
		star< sor< alnum, space> >,
		one< '\"', '\'' >
	> { };
	struct Error : seq< 
		TAO_PEGTL_ISTRING("error"),
		one< '[' >,
		QuotedString,
		opt_must<
			one< ',' >,
			Error
		>,
		one< ']' >
	> { };

	ParserProc<int> parse_error()
	{
		auto tk = co_await NextToken;
		while (tk.has_value())
		{
			std::cout << "In coroutine: " << tk.value() << std::endl;
			tk = co_await NextToken;
		}
		co_return 0;
	}

	template < class Rule > struct Action : nothing< Rule > { };

	template < > struct Action< Error >
	{
		template < class ActionInput, class Coro >
		static void apply(const ActionInput& in, Coro& coro)
		{
			// std::cout << in.string() << std::endl;
			coro.push_token(in.string());
		}
	};
	// template < > struct Action< string<'e', 'r', 'r', 'o', 'r'> >
	// {
	// 	template < class ActionInput >
	// 	static void apply(const ActionInput& in)
	// 	{
	// 		std::cout << in.string() << std::endl;
	// 	}
	// };
}

int main()
{
	using namespace coroparse;
	using namespace tao::pegtl;

	// auto p = pp(2);
	// p.push_token("JS Bach");
	// p.push_token("G. Mahler!");
	// std::cout << p.result() << std::endl;

	auto f = ffa(2);
	// f.__dbg_print_coro_stack();
	f.push_value("Js Bach");
	f.push_value("Mahler");
	f.result();
	std::cout << f.result() << std::endl;

	string_input in("error['low pressure',error['next err']]", "");
	auto coro = ex::parse_error();
	parse< ex::Error, ex::Action >(in, coro);
	coro.result();

	return 0;
}
