#pragma once

#include <string_view>
#include <coroutine>
#include <tao/pegtl.hpp>

namespace coroparse
{
	struct NextTokenT { };
	inline extern NextTokenT NextToken = NextTokenT{ };

	template< class T >
	struct ParserProc
	{
		struct Promise;
		using promise_type = Promise;

		struct Promise
		{
			using CoroHandle = std::coroutine_handle< Promise >;

			ParserProc<T> get_return_object() 
			{
				return { CoroHandle::from_promise(*this) };
			}
			void unhandled_exception() { ex_ptr = std::current_exception(); }
			void return_value(T&& ret_) { ret = std::forward<T>(ret_); }

			auto initial_suspend() noexcept { return std::suspend_never { }; }

			auto final_suspend() noexcept { return std::suspend_always { }; }

			bool push_token(std::string_view tk) // --> token_delivered
			{
				auto coro_handle = CoroHandle::from_promise(*this);
				if (coro_handle.done()) assert(false && "This probably happens because the coroutine dies before all token are pumped into it.");

				if (expecting_token)
				{
					assert(!token.has_value());
					token = tk;
					coro_handle.resume();
					return true;
				}
				else
				{
					if (!inferior_coro_handle)
					{
						// Reaching here means the child coro is finished (i.e. this coro is leaf) but the 
						// current coro is not ready to consume any token -> Simply advances to the next state.
						coro_handle.resume();
						return false;
					}
					else
					{
						if (inferior_coro_handle.done())
						{
							inferior_coro_handle = nullptr;
							coro_handle.resume();
							return false;
						}
						else return inferior_coro_handle.promise().push_token(tk);
					}
				}
			}

			bool wind_until_end_state() // --> is_done?
			{
				auto coro_handle = CoroHandle::from_promise(*this);
				if (inferior_coro_handle)
				{
					if (inferior_coro_handle.promise().wind_until_end_state())
					{
						// if child is done, detach
						inferior_coro_handle = nullptr;
						return coro_handle.done();
					}
					else
					{
						return false;
					}
				}
				else // is_leaf
				{
					if (coro_handle.done()) return true;
					coro_handle.resume();
					return coro_handle.done();
				}
			}

			auto await_transform(ParserProc<T>&& parse_proc)
			{
				inferior_coro_handle = parse_proc.coro_handle;
				struct Awaiter
				{
					CoroHandle inferior_coro_handle = nullptr;
					bool await_ready() { return false; }
					bool await_suspend( std::coroutine_handle< Promise > )
					{
						return true;
					}
					decltype(auto) await_resume()
					{
						return std::forward<decltype(inferior_coro_handle.promise().ret)>(inferior_coro_handle.promise().ret);
					}
				};
				return Awaiter{ .inferior_coro_handle = parse_proc.coro_handle };
			}
			struct NextTokenAwaitable // Extracts and return the token that `push_token` has placed into this coro's promise.
			{
				Promise* promise = nullptr;
				bool await_ready() { return false; }
				bool await_suspend( CoroHandle coro_handle_ )
				{
					promise = std::addressof(coro_handle_.promise());
					promise->token = std::nullopt;
					promise->expecting_token = true;
					return true;
				}
				auto await_resume()
				{
					assert(promise->expecting_token);

					promise->expecting_token = false;
					return promise->token;
				}
			};
			auto await_transform(NextTokenT)
			{
				return NextTokenAwaitable{ };
			}

		private:
			friend struct NextTokenAwaitable;
			friend struct ParserProc<T>;

			bool expecting_token = false;
			std::optional< std::string_view > token = std::nullopt;
			std::exception_ptr ex_ptr = nullptr;

			T ret;
			CoroHandle inferior_coro_handle = nullptr;
		};

		T result()
		{
			if (!coro_handle.done())
			{
				bool done = false;
				do {
					done = coro_handle.promise().wind_until_end_state();
				} while (!done);
			}
			if (coro_handle.promise().ex_ptr)
			{
				std::rethrow_exception(coro_handle.promise().ex_ptr);
			}
			return coro_handle.promise().ret;
		}
		void push_token(std::string_view tk)
		{
			bool delivered = false;
			int __dbg_idx = 1;
			// Keeps on winding the state machine until the token is consumed.
			do {
				delivered = coro_handle.promise().push_token(tk);
				++__dbg_idx;
			} while (!delivered);
		}

		ParserProc(std::coroutine_handle< Promise > ch_) : coro_handle { ch_ } { }
		~ParserProc() { if (coro_handle) coro_handle.destroy(); }
		
	private:
		std::coroutine_handle< Promise > coro_handle = nullptr;
	};

	ParserProc<int> pp(int a)
	{
		if (a == 0)
		{
			std::cout << "In recursion: " << a << std::endl;

			auto tk = co_await NextTokenT{};
			std::cout << "In parser_proc: " << tk.value() << std::endl;
			// auto tk1 = co_await NextTokenT{};
			// std::cout << "In parser_proc: " << tk1 << std::endl;
			
			co_return 1;
		}
		else
		{
			std::cout << "Descend: " << a << std::endl;
			int r = co_await pp(a - 1);
			auto tk = co_await NextTokenT{};
			if (tk.has_value()) std::cout << "In parser_proc: " << tk.value() << std::endl;
		}

		co_return 100;
	}

}