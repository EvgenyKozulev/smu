#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/allocators.h>
#include "smu.h"
#include <iostream>
#include <cstdio>

// =============================================================================
// TRAP (ЛОВУШКА)
// =============================================================================
static bool trap_enabled = false;

void *operator new(std::size_t size)
{
	if (trap_enabled) {
		std::fprintf(
			stderr,
			"\n!!! CRITICAL: SYSTEM ALLOCATION (%zu bytes) !!!\n",
			size);
		std::abort();
	}
	return std::malloc(size);
}

void operator delete(void *ptr) noexcept
{
	std::free(ptr);
}
void operator delete(void *ptr, std::size_t) noexcept
{
	std::free(ptr);
}

// =============================================================================
// RAPIDJSON ADAPTER
// =============================================================================
struct SmuAllocContext {
	static inline Smu *current_smu = nullptr;
};

class SmuRapidAllocator : public SmuAllocContext {
    public:
	static const bool kNeedFree = true;

	void *Malloc(size_t size)
	{
		if (!size)
			return nullptr;
		return current_smu->allocate(size);
	}

	void *Realloc(void *old_ptr, size_t old_size, size_t new_size)
	{
		if (new_size == 0) {
			if (old_ptr)
				current_smu->deallocate(old_ptr);
			return nullptr;
		}
		void *new_ptr = current_smu->allocate(new_size);
		if (old_ptr && new_ptr) {
			std::memcpy(new_ptr, old_ptr,
				    std::min(old_size, new_size));
			current_smu->deallocate(old_ptr);
		}
		return new_ptr;
	}

	static void Free(void *ptr)
	{
		if (ptr && current_smu)
			current_smu->deallocate(ptr);
	}
};

using SmuDocument =
	rapidjson::GenericDocument<rapidjson::UTF8<>, SmuRapidAllocator>;
using SmuValue = rapidjson::GenericValue<rapidjson::UTF8<>, SmuRapidAllocator>;

// =============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ГЕНЕРАЦИИ
// =============================================================================

void GenerateSterile(SmuValue &parent, int depth,
		     SmuDocument::AllocatorType &alloc)
{
	if (depth <= 0) {
		parent.SetInt(std::rand() % 1000);
		return;
	}

	parent.SetObject();

	for (int i = 0; i < 2; ++i) {
		char key_buf[32];
		int len = std::snprintf(key_buf, sizeof(key_buf),
					"node_d%d_i%d", depth, i);

		SmuValue key(key_buf, static_cast<rapidjson::SizeType>(len),
			     alloc);

		SmuValue value;
		if (depth > 1) {
			GenerateSterile(value, depth - 1, alloc);
		} else {
			value.SetInt(std::rand() % 100);
		}

		parent.AddMember(key, value, alloc);
	}
}

TEST(SmuJsonTest, AbsoluteZeroAllocationRapid)
{
	const size_t POOL_SIZE = 64 * 1024;
	alignas(16) static std::byte static_pool[POOL_SIZE];

	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	{
		SmuRapidAllocator allocator;
		trap_enabled = true;

		SmuDocument d(&allocator);
		d.SetObject();

		d.AddMember("project", "SMU_STRICT_MODE", d.GetAllocator());

		SmuValue metrics(rapidjson::kObjectType);
		metrics.AddMember("integrity", "hardcore", d.GetAllocator());
		metrics.AddMember("status", "sterile", d.GetAllocator());

		d.AddMember("metrics", metrics, d.GetAllocator());

		std::cout << "SMU Busy Bytes: " << smu.busyBytes() << std::endl;

		trap_enabled = false;

		EXPECT_GT(smu.busyBytes(), 0);
		EXPECT_TRUE(smu.checkIntegrity());
	}

	SmuAllocContext::current_smu = nullptr;
	EXPECT_EQ(smu.busyNodes(), 0);
}

TEST(SmuJsonTest, StressGenerationAndSerialization)
{
	const size_t POOL_SIZE = 128 * 1024;
	alignas(16) static std::byte static_pool[POOL_SIZE];

	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	{
		SmuRapidAllocator allocator;
		SmuDocument d(&allocator);

		trap_enabled = true;

		GenerateSterile(d, 4, d.GetAllocator());

		rapidjson::GenericStringBuffer<rapidjson::UTF8<>,
					       SmuRapidAllocator>
			buffer(&allocator);

		rapidjson::Writer<rapidjson::GenericStringBuffer<
					  rapidjson::UTF8<>, SmuRapidAllocator>,
				  rapidjson::UTF8<>, rapidjson::UTF8<>,
				  SmuRapidAllocator>
			writer(buffer, &allocator);

		d.Accept(writer);

		std::cout << "\n[ STRESS TEST REPORT ]" << std::endl;
		std::cout << "JSON Result Size: " << buffer.GetSize()
			  << " bytes" << std::endl;
		std::cout << "SMU Busy Bytes:   " << smu.busyBytes()
			  << std::endl;

		trap_enabled = false;

		EXPECT_GT(buffer.GetSize(), 0);
		EXPECT_TRUE(smu.checkIntegrity());
	}

	SmuAllocContext::current_smu = nullptr;
	EXPECT_EQ(smu.busyNodes(), 0);
}

TEST(SmuJsonTest, LargeKeysHeavyFill)
{
	const size_t POOL_SIZE = 128 * 1024;
	alignas(16) static std::byte static_pool[POOL_SIZE];

	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	{
		SmuRapidAllocator allocator;
		SmuDocument d(&allocator);
		d.SetObject();

		trap_enabled = true;

		for (int i = 0; i < 100; ++i) {
			char key_buf[64];
			int len = std::snprintf(
				key_buf, sizeof(key_buf),
				"key_index_%03d_extra_padding_for_stress_test",
				i);

			SmuValue key(key_buf,
				     static_cast<rapidjson::SizeType>(len),
				     d.GetAllocator());

			char val_buf[32];
			int v_len = std::snprintf(val_buf, sizeof(val_buf),
						  "value_data_%d", i * 7);
			SmuValue val(val_buf,
				     static_cast<rapidjson::SizeType>(v_len),
				     d.GetAllocator());

			d.AddMember(key, val, d.GetAllocator());
		}

		std::cout << "Members added: " << d.MemberCount() << std::endl;
		std::cout << "SMU Busy Bytes: " << smu.busyBytes()
			  << " (Data + Nodes)" << std::endl;
		std::cout << "SMU Busy Nodes: " << smu.busyNodes() << std::endl;

		trap_enabled = false;

		EXPECT_EQ(d.MemberCount(), 100);
		EXPECT_TRUE(smu.checkIntegrity());
	}

	EXPECT_EQ(smu.busyNodes(), 0);
	SmuAllocContext::current_smu = nullptr;
}
