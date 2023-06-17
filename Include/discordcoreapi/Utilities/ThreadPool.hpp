/*
	DiscordCoreAPI, A bot library for Discord, written in C++, and featuring explicit multithreading through the usage of custom, asynchronous C++ CoRoutines.

	Copyright 2021, 2022, 2023 Chris M. (RealTimeChris)

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
	USA
*/
/// ThreadPool.hpp - Header for the "Thread-Pool" related stuff.
/// Dec 18, 2021
/// https://discordcoreapi.com
/// \file ThreadPool.hpp

#pragma once

#include <discordcoreapi/FoundationEntities.hpp>
#include <discordcoreapi/Https.hpp>
#include <coroutine>

namespace DiscordCoreAPI {
	/**
	 * \addtogroup utilities
	 * @{
	 */

	template<typename... ArgTypes> using TimeElapsedHandler = std::function<void(ArgTypes...)>;

	using TimeElapsedHandlerNoArgs = std::function<void(void)>;

	inline static constexpr double percentage{ 10.0f / 100.0f };

	class ThreadPool {
	  public:
		inline ThreadPool& operator=(const ThreadPool&) = delete;

		inline ThreadPool(const ThreadPool&) = delete;

		inline ThreadPool() noexcept = default;

		inline static std::string storeThread(TimeElapsedHandlerNoArgs timeElapsedHandler, int64_t timeInterval) {
			std::string threadId = std::to_string(std::chrono::duration_cast<Nanoseconds>(HRClock::now().time_since_epoch()).count());

			auto thread = std::jthread([=](std::stop_token token) {
				StopWatch stopWatch{ Milliseconds{ timeInterval } };
				while (true) {
					stopWatch.resetTimer();
					std::this_thread::sleep_for(Milliseconds{ static_cast<int64_t>(std::ceil(static_cast<double>(timeInterval) * percentage)) });
					while (!stopWatch.hasTimePassed() && !token.stop_requested()) {
						std::this_thread::sleep_for(1ms);
					}
					if (token.stop_requested()) {
						return;
					}
					timeElapsedHandler();
					if (token.stop_requested()) {
						return;
					}
					std::this_thread::sleep_for(1ms);
				}
			});
			thread.detach();
			threads[threadId] = std::move(thread);
			return threadId;
		}

		template<typename... ArgTypes> inline static void executeFunctionAfterTimePeriod(TimeElapsedHandler<ArgTypes...> timeElapsedHandler,
			int64_t timeDelay, bool blockForCompletion, ArgTypes... args) {
			std::jthread thread = std::jthread([=](std::stop_token token) {
				StopWatch stopWatch{ Milliseconds{ timeDelay } };
				stopWatch.resetTimer();
				if (static_cast<int64_t>(std::ceil(static_cast<double>(timeDelay) * percentage)) <= timeDelay &&
					static_cast<int64_t>(std::ceil(static_cast<double>(timeDelay) * percentage)) > 0) {
					std::this_thread::sleep_for(Milliseconds{ static_cast<int64_t>(std::ceil(static_cast<double>(timeDelay) * percentage)) });
				}
				while (!stopWatch.hasTimePassed() && !token.stop_requested()) {
					std::this_thread::sleep_for(1ms);
				}
				if (token.stop_requested()) {
					return;
				}
				timeElapsedHandler(args...);
				if (token.stop_requested()) {
					return;
				}
			});
			if (blockForCompletion) {
				if (thread.joinable()) {
					thread.join();
				}
			} else if (thread.joinable()) {
				thread.detach();
			}
		}

		inline void stopThread(const std::string& key) {
			if (ThreadPool::threads.contains(key)) {
				ThreadPool::threads[key].request_stop();
				ThreadPool::threads.erase(key);
			}
		}

		inline ~ThreadPool() noexcept = default;

	  protected:
		inline static std::unordered_map<std::string, std::jthread> threads{};
	};
}

namespace DiscordCoreInternal {

	struct WorkerThread {
		inline WorkerThread& operator=(WorkerThread&& other) noexcept {
			if (this != &other) {
				areWeCurrentlyWorking.store(other.areWeCurrentlyWorking.load());
				tasks = std::move(other.tasks);
				thread.swap(other.thread);
			}
			return *this;
		}

		inline WorkerThread() noexcept = default;

		inline ~WorkerThread() noexcept = default;

		DiscordCoreAPI::UnboundedMessageBlock<std::coroutine_handle<>> tasks{};
		std::atomic_bool areWeCurrentlyWorking{};
		std::jthread thread{};
	};

	class CoRoutineThreadPool {
	  public:
		friend class DiscordCoreAPI::DiscordCoreClient;

		inline CoRoutineThreadPool() : threadCount(std::thread::hardware_concurrency()) {
			for (uint32_t x = 0; x < threadCount; ++x) {
				WorkerThread workerThread{};
				currentIndex.store(currentIndex.load() + 1);
				currentCount.store(currentCount.load() + 1);
				int64_t indexNew = currentIndex.load();
				workerThread.thread = std::jthread([=, this](std::stop_token stopToken) {
					threadFunction(stopToken, indexNew);
				});
				workerThreads[currentIndex.load()] = std::move(workerThread);
			}
		}

		inline void submitTask(std::coroutine_handle<> coro) noexcept {
			bool areWeAllBusy{ true };
			size_t currentLowestValue{ std::numeric_limits<size_t>::max() };
			size_t currentLowestIndex{ std::numeric_limits<size_t>::max() };
			std::shared_lock lock01{ workerAccessMutex };
			for (auto& [key, value]: workerThreads) {
				if (!value.areWeCurrentlyWorking.load()) {
					areWeAllBusy = false;
					if (value.tasks.size() < currentLowestValue) {
						currentLowestValue = value.tasks.size();
						currentLowestIndex = key;
					}
					break;
				}
			}
			if (areWeAllBusy) {
				WorkerThread workerThread{};
				currentIndex.store(currentIndex.load() + 1);
				currentCount.store(currentCount.load() + 1);
				int64_t indexNew = currentIndex.load();
				workerThread.thread = std::jthread([=, this](std::stop_token stopToken) {
					threadFunction(stopToken, indexNew);
				});
				lock01.unlock();
				std::unique_lock lock02{ workerAccessMutex };
				workerThreads[currentIndex.load()] = std::move(workerThread);
				lock02.unlock();
			} else {
				workerThreads[currentLowestIndex].tasks.send(std::move(coro));
			}
		}

	  protected:
		std::unordered_map<int64_t, WorkerThread> workerThreads{};
		std::shared_mutex workerAccessMutex{};
		std::atomic_int64_t currentCount{};
		std::atomic_int64_t currentIndex{};
		const int64_t threadCount{};

		inline void threadFunction(std::stop_token stopToken, int64_t index) {
			while (!stopToken.stop_requested()) {
				std::coroutine_handle<> coroHandle{};
				if (workerThreads[index].tasks.tryReceive(coroHandle)) {
					workerThreads[index].areWeCurrentlyWorking.store(true);
					try {
						coroHandle();
					} catch (const HttpsError& error) {
						Globals::MessagePrinter::printError<Globals::MessageType::Https>(error.what());
					} catch (const DiscordCoreAPI::DCAException& error) {
						Globals::MessagePrinter::printError<Globals::MessageType::General>(error.what());
					}
					workerThreads[index].areWeCurrentlyWorking.store(false);
				}
				if (currentCount.load() > threadCount) {
					int64_t extraWorkers{ currentCount.load() - threadCount };
					while (extraWorkers > 0) {
						--extraWorkers;
						std::unique_lock lock{ workerAccessMutex };
						const auto oldThread = workerThreads.begin();
						if (oldThread->second.thread.joinable() && oldThread->second.areWeCurrentlyWorking.load()) {
							oldThread->second.thread.request_stop();
							oldThread->second.thread.detach();
							currentCount.store(currentCount.load() - 1);
							workerThreads.erase(oldThread->first);
						}
					}
				}
				std::this_thread::sleep_for(100000ns);
			}
		}
	};
	/**@}*/
}
