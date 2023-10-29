#pragma once

#include <string_view>
#include <coroutine>
#include <tao/pegtl.hpp>

namespace coroparse
{
	struct NextTokenT { };
	inline extern NextTokenT NextToken = NextTokenT{ };
	struct EndTokenT { };
	inline extern EndTokenT EndToken = EndTokenT{ };

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

	template< class R, class T >
	struct Degenerator_
	{
		struct Promise;
		using promise_type = Promise;
		using CoroHandle = std::coroutine_handle< Promise >;

		struct Promise
		{
			Degenerator_<R, T> get_return_object()
			{
				return Degenerator_<R, T>{ CoroHandle::from_promise(*this) };
			}
			void unhandled_exception() { ex_ptr = std::current_exception(); }
			void return_value(R&& ret_) { ret = std::forward<R>(ret_); }

			struct InitialAwaitable
			{
				Promise* self = nullptr;
				bool await_ready() noexcept { return false; }
				bool await_suspend(CoroHandle parent_coro) noexcept
				{
					if (std::addressof(parent_coro.promise()) == self)
					{
						self->parent_promise = nullptr;
						self->top_stack_or_base = self;
					}
					else
					{
						self->parent_promise = std::addressof(parent_coro.promise());
						self->top_stack_or_base = parent_coro.promise().get_base();
						self->get_base()->top_stack_or_base = self;
					}

					return false; 
				}
				auto await_resume() noexcept
				{
				}
			};
			auto initial_suspend() noexcept 
			{ 
				// Give parent a chance to register this coro.
				return std::suspend_always { }; 
			}

			struct FinalAwaitable
			{
				bool await_ready() noexcept { return false; }
				bool await_suspend( CoroHandle dying_coro ) noexcept
				{
					return true; 
				}
				auto await_resume() noexcept
				{
				}
			};
			auto final_suspend() noexcept 
			{ 
				// return FinalAwaitable { }; 
				return std::suspend_always { };
			}

			bool done() { return CoroHandle::from_promise(*this).done(); }
			void resume() { return CoroHandle::from_promise(*this).resume(); }

			void push_value(T& value)
			{
				// `this` should be top_stack
				assert(!get_top()->token);
				get_top()->token = std::addressof(value);
				get_top()->resume();
			}

			struct NextTokenAwaitable
			{
				Promise* promise = nullptr;
				bool await_ready() { return false; }
				void await_suspend( CoroHandle coro ) 
				{
					promise = std::addressof(coro.promise());
					promise->token = nullptr;
					promise->expecting_token = true;
				}
				auto await_resume()
				{
					promise->expecting_token = false;
					return std::exchange(promise->token, nullptr);
				}
			};
			auto await_transform(NextTokenT)
			{
				return NextTokenAwaitable { };
			}
			struct Awaitable 
			{ 
				Promise* self_promise = nullptr;
				bool await_ready() noexcept { return false; }
				bool await_suspend(CoroHandle parent_coro) noexcept
				{
					return true;
				}
				auto await_resume() 
				{
					return self_promise->result();
				}
			};
			auto await_transform(Degenerator_<R, T>&& dg)
			{
				Promise& child_promise = dg.coro_handle.promise();
				child_promise.parent_promise = this;
				child_promise.top_stack_or_base = this->get_base();
				child_promise.get_base()->top_stack_or_base = std::addressof(child_promise);

				dg.coro_handle.resume(); // Move past initial suspend

			 	return Awaitable { .self_promise = std::addressof(dg.coro_handle.promise()) };
			}

			R result()
			{
				if (ex_ptr)
				{
					std::rethrow_exception(ex_ptr);
				}
				return ret;
			}

			// Continue until the next state which can accept a token.
			// Start at the top of the async stack then work backwards.
			bool seek_accepting_state()
			{
				if (!done() && !expecting_token) resume();
				return expecting_token;
			}

			// private:
			std::exception_ptr ex_ptr = nullptr;
			R ret;
			T* token = nullptr;
			bool expecting_token = false;

			// For the base, this points to nothing.
			// For everything else, this points to the parent promise.
			Promise* parent_promise = nullptr;
			// For the base, this points to `top_stack`.
			// For everything else, this points to the `base`.
			Promise* top_stack_or_base = nullptr;

			bool is_base() { return !parent_promise; }
			Promise* get_base() { return top_stack_or_base; }
			Promise* get_top() { return top_stack_or_base; }
		};

		void seek_accepting_state()
		{
			while (!coro_handle.promise().get_top()->expecting_token
				&& !coro_handle.promise().get_top()->done())
			{
			}
		}

		auto push_value(T& value)
		{
			if (!moved_past_initial_suspend) [[unlikely]]
				move_past_initial_suspend();
			seek_accepting_state();
			return coro_handle.promise().push_value(value);
		}

		R result()
		{
			return coro_handle.promise().result();
		}

		Degenerator_(CoroHandle coro) : coro_handle { coro } 
		{
			coro_handle.promise().parent_promise = nullptr;
			coro_handle.promise().top_stack_or_base = std::addressof(coro_handle.promise());
		}
		~Degenerator_() { if (coro_handle) coro_handle.destroy(); }


		void move_past_initial_suspend()
		{
			coro_handle.resume();
			moved_past_initial_suspend = true;
		}
		void __dbg_print_coro_stack()
		{
			if (!moved_past_initial_suspend) [[unlikely]]
				move_past_initial_suspend();
			Promise* p = coro_handle.promise().top_stack_or_base;
			while (p)
			{
				std::printf("Promise: %p\n", p);
				p = coro_handle.promise().parent_promise;
			}
		}
	private:
		CoroHandle coro_handle = nullptr;
		bool moved_past_initial_suspend = false;
	};

	Degenerator_<int, const std::string_view> ff(int a)
	{
		if (a == 0)
		{
			std::cout << "In recursion: " << a << std::endl;

			auto tk = co_await NextTokenT{};
			std::cout << "In parser_proc: " << *tk << std::endl;
			// auto tk1 = co_await NextTokenT{};
			// std::cout << "In parser_proc: " << *tk1 << std::endl;
			
			co_return 1;
		}
		else
		{
			std::cout << "Descend: " << a << std::endl;
			auto r = co_await ff(a - 1);
			auto tk = co_await NextTokenT{};
			if (tk) std::cout << "In parser_proc: " << *tk << std::endl;
			std::cout << "Ret val: " << r << std::endl;
		}

		co_return 100;
	}



	template < class R, class T >
	struct Degenerator
	{
		struct Promise
		{
			using CoroHandle = std::coroutine_handle< Promise >;
			Degenerator< R, T > get_return_object()
			{
				return Degenerator<R, T>{ CoroHandle::from_promise(*this) };
			}
			void unhandled_exception() { eptr = std::current_exception(); }
			void return_value(R&& ret_) { ret = std::forward<R>(ret_); }

			bool done() { return CoroHandle::from_promise(*this).done(); }
			void resume() { return CoroHandle::from_promise(*this).resume(); }

			// If self is base, the behavior is undefined.
			Promise*& get_top_non_base() { return base_or_top->base_or_top; }
			Promise*& get_top_as_base() { return base_or_top; }
			Promise*& get_base() { return base_or_top; }
			bool is_base() 
			{ 
				return !prev;
			}

			auto initial_suspend() noexcept { return std::suspend_always{ }; }
			struct FinalAwaitable
			{
				bool await_ready() noexcept { return false; }
				bool await_suspend(CoroHandle dying_coro) noexcept
				{
					// Final suspend of the last frame.
					if (dying_coro.promise().prev == nullptr) return true; 

					Promise& parent_promise = *dying_coro.promise().prev;
					if (parent_promise.is_base())
						parent_promise.get_top_as_base() = std::addressof(parent_promise);
					else
						parent_promise.get_top_non_base() = std::addressof(parent_promise);
					return true;
				}
				auto await_resume() noexcept
				{
				}
			};
			auto final_suspend() noexcept { return FinalAwaitable { }; }

			auto await_transform(Degenerator<R, T>&& dg)
			{
				Promise& child_promise = dg.handle.promise();
				child_promise.prev = this;
				child_promise.get_base() = this->get_base(); // Should be pointing to base here.
				child_promise.get_base()->get_top_as_base() = std::addressof(child_promise);
			
				struct Awaitable
				{
					Promise* promise = nullptr;
					bool await_ready() { return false; }
					bool await_suspend(CoroHandle) { return true; }
					auto await_resume() { return promise->result(); }
				};
				return Awaitable{ std::addressof(dg.handle.promise()) };
			}

			auto await_transform(NextTokenT)
			{
				struct NextTokenAwaitable
				{
					Promise* promise = nullptr;
					bool await_ready() { return false; }
					void await_suspend(CoroHandle coro)
					{
						promise = std::addressof(coro.promise());
						promise->is_expecting_token = true;
						promise->token = nullptr;
					}
					auto await_resume()
					{
						promise->is_expecting_token = false;
						return std::exchange(promise->token, nullptr);
					}
				};
				return NextTokenAwaitable{ };
			}

			auto result()
			{
				if (eptr) std::rethrow_exception(eptr);
				return ret;
			}

			bool is_expecting_token = false;
		protected:
			friend struct Degenerator;

			Promise* prev = nullptr; // Points to previous stack frame. For base, this points to nullptr For base, this points to nullptr.
			// For the base, this points to `top`.
			// For everything else, this field points to base.
			// Thus, for non-base, the top can be get at via base_or_top->base_or_top.
			Promise* base_or_top = nullptr; 

			T* token = nullptr;
			std::exception_ptr eptr = nullptr;
			R ret;
		};

		using promise_type = Promise;
		using CoroHandle = std::coroutine_handle< promise_type >;

		Promise* seek_accepting_state()
		{
			Promise* top_promise = handle.promise().get_top_as_base();
			// Could have been resumed and thus done by now so must check
			// if (!top_promise->prev && top_promise->done()) return nullptr;
			while (top_promise && !top_promise->is_expecting_token)
			{
				if (!top_promise->done())
				{
					top_promise->resume();
					top_promise = handle.promise().get_top_as_base();
				}
				else
				{
					top_promise = top_promise->prev;
				}
			}
			return top_promise;
		}

		void push_value(T& value)
		{
			Promise* acceptor = seek_accepting_state();
			acceptor->token = std::addressof(value);
			acceptor->resume();
		}
		void push_value(EndTokenT)
		{
			Promise* acceptor = seek_accepting_state();
			do 
			{ 
				if (!acceptor) break;
				acceptor->token = nullptr;
				acceptor->resume();
				acceptor = seek_accepting_state(); 
			} 
			while (acceptor);
		}

		R result() 
		{ 
			push_value(EndToken);
			return handle.promise().result(); 
		}

		Degenerator(CoroHandle handle_) : handle { handle_ } 
		{
			handle.promise().prev = nullptr;
			handle.promise().base_or_top = std::addressof(handle.promise());
		}
		~Degenerator() { if (handle) handle.destroy(); }
	protected:
		CoroHandle handle = nullptr;
	};

	Degenerator<int, const std::string_view> ffa(int a)
	{
		if (a == 0)
		{
			std::cout << "In recursion: " << a << std::endl;

			auto tk = co_await NextTokenT{};
			std::cout << "In parser_proc: " << *tk << std::endl;
			auto tk1 = co_await NextTokenT{};
			std::cout << "In parser_proc: " << *tk1 << std::endl;
			
			co_return 1;
		}
		else
		{
			std::cout << "Descend: " << a << std::endl;
			auto r = co_await ffa(a - 1);
			auto tk = co_await NextTokenT{};
			if (tk) std::cout << "In parser_proc: " << *tk << std::endl;
			std::cout << "Ret val: " << r << std::endl;
		}

		co_return 100;
	}

}