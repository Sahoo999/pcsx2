#include "common/Threading.h"

#include <gtest/gtest.h>

#include <atomic>
#include <bit>

#ifdef _WIN32

#include <windows.h>

TEST(ThreadHandle, SetAffinityTargetsHandle)
{
	GROUP_AFFINITY caller_affinity = {};
	ASSERT_TRUE(GetThreadGroupAffinity(GetCurrentThread(), &caller_affinity, nullptr));

	const u64 available_mask = static_cast<u64>(caller_affinity.Mask);

	if (std::popcount(available_mask) < 2)
		GTEST_SKIP() << "At least two processors are required for this test.";

	const u32 first_cpu = std::countr_zero(available_mask);
	const u64 remaining_mask = available_mask & (available_mask - 1);
	const u32 second_cpu = std::countr_zero(remaining_mask);

	const DWORD_PTR caller_mask = static_cast<DWORD_PTR>(u64(1) << first_cpu);
	const DWORD_PTR worker_mask = static_cast<DWORD_PTR>(u64(1) << second_cpu);

	std::atomic<bool> worker_ready{false};
	std::atomic<bool> worker_failed{false};
	std::atomic<bool> check_worker_affinity{false};
	std::atomic<bool> worker_checked{false};
	std::atomic<u64> worker_observed_mask{0};

	Threading::Thread worker([&]() {
		if (SetThreadAffinityMask(GetCurrentThread(), worker_mask) == 0)
		{
			worker_failed.store(true, std::memory_order_release);
			worker_ready.store(true, std::memory_order_release);
			return;
		}

		worker_ready.store(true, std::memory_order_release);

		while (!check_worker_affinity.load(std::memory_order_acquire))
			Threading::Sleep(1);

		GROUP_AFFINITY affinity = {};
		if (GetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr))
			worker_observed_mask.store(
				static_cast<u64>(affinity.Mask), std::memory_order_release);

		worker_checked.store(true, std::memory_order_release);
	});

	while (!worker_ready.load(std::memory_order_acquire))
		Threading::Sleep(1);

	ASSERT_FALSE(worker_failed.load(std::memory_order_acquire));

	GROUP_AFFINITY caller_before = {};
	ASSERT_TRUE(GetThreadGroupAffinity(GetCurrentThread(), &caller_before, nullptr));
	EXPECT_EQ(static_cast<u64>(caller_before.Mask), available_mask);

	// Set the worker's affinity through its ThreadHandle.
	ASSERT_TRUE(worker.GetThreadHandle().SetAffinity(caller_mask));

	// The calling thread must not have been modified.
	GROUP_AFFINITY caller_after = {};
	ASSERT_TRUE(GetThreadGroupAffinity(GetCurrentThread(), &caller_after, nullptr));
	EXPECT_EQ(static_cast<u64>(caller_after.Mask), available_mask);

	check_worker_affinity.store(true, std::memory_order_release);

	while (!worker_checked.load(std::memory_order_acquire))
		Threading::Sleep(1);

	EXPECT_EQ(worker_observed_mask.load(std::memory_order_acquire),
		static_cast<u64>(caller_mask));

	worker.Join();
}

#endif // _WIN32