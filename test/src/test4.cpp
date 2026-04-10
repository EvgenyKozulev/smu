#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/allocators.h>
#include "smu.h"
#include <iostream>
#include <cstdio>
#include <chrono>
#include <ratio>
#include <ctime>

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

TEST(SmuJsonTest, BenchmarkVsMallocLimitedHeap)
{


	const size_t POOL_SIZE = 256 * 1024; // Ограничиваем SMU пул
	alignas(16) static std::byte static_pool[POOL_SIZE];

	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	auto start_smu = std::clock();

	trap_enabled = true;
	{
		SmuRapidAllocator allocator;
		SmuDocument d(&allocator);
		d.SetObject();

		// Увеличиваем нагрузку для точного измерения
		for (int i = 0; i < 200; ++i) {
			char key[32];
			std::sprintf(key, "key_%03d_with_longer_name", i);
			d.AddMember(SmuValue(key, d.GetAllocator()).Move(),
				    SmuValue(i).Move(), d.GetAllocator());
		}

		rapidjson::GenericStringBuffer<rapidjson::UTF8<>, SmuRapidAllocator> buffer(&allocator);
		rapidjson::Writer<rapidjson::GenericStringBuffer<rapidjson::UTF8<>, SmuRapidAllocator>,
				  rapidjson::UTF8<>, rapidjson::UTF8<>, SmuRapidAllocator>
			writer(buffer, &allocator);
		d.Accept(writer);
	}
	trap_enabled = false;

	auto end_smu = std::clock();
	auto duration_smu = std::chrono::microseconds(static_cast<long long>(
		(static_cast<double>(end_smu - start_smu) / CLOCKS_PER_SEC) * 1000000.0));

	// Теперь malloc с ограниченным heap
	auto start_malloc = std::clock();

	{
		rapidjson::Document d;
		for (int i = 0; i < 200; ++i) {
			rapidjson::Value obj(rapidjson::kObjectType);
			char key[32];
			std::sprintf(key, "key_%03d_with_longer_name", i);
			obj.AddMember(rapidjson::Value(key, d.GetAllocator()).Move(),
				      rapidjson::Value(i), d.GetAllocator());
		}

		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		d.Accept(writer);
	}

	auto end_malloc = std::clock();
	auto duration_malloc = std::chrono::microseconds(static_cast<long long>(
		(static_cast<double>(end_malloc - start_malloc) / CLOCKS_PER_SEC) * 1000000.0));

	std::cout << "\n--- LIMITED HEAP BENCHMARK REPORT ---" << std::endl;
	std::cout << "[SMU Allocator] Time: " << duration_smu.count() << " μs (Pool: " << POOL_SIZE / 1024 << " KB)" << std::endl;
	std::cout << "[System Malloc] Time: " << duration_malloc.count() << " μs (Limited heap)" << std::endl;
	if (duration_malloc.count() > 0) {
		std::cout << "[Ratio] SMU/Malloc: " << (double)duration_smu.count() / duration_malloc.count() << std::endl;
	} else {
		std::cout << "[Ratio] SMU/Malloc: N/A (malloc too fast)" << std::endl;
	}

	SmuAllocContext::current_smu = nullptr;
	EXPECT_EQ(smu.busyNodes(), 0);
}

TEST(SmuJsonTest, EmbeddedStyleStressTest)
{
	// Симуляция embedded: маленький пул, много мелких аллокаций
	const size_t POOL_SIZE = 16 * 1024; // 16KB - типично для embedded
	alignas(16) static std::byte static_pool[POOL_SIZE];

	Smu smu(8, 32, std::span<std::byte>(static_pool, POOL_SIZE)); // Маленькие блоки
	SmuAllocContext::current_smu = &smu;

	// Используем статический массив вместо std::vector (избегаем системных аллокаций)
	void* ptrs[100];
	int allocation_count = 0;

	auto start = std::clock();

	// Много мелких аллокаций (embedded паттерн)
	for (int i = 0; i < 100; ++i) {
		void* ptr = smu.allocate(16 + (i % 16)); // Разные размеры
		if (ptr) {
			ptrs[allocation_count++] = ptr;
		} else {
			break; // Пул исчерпан
		}
	}

	// Освобождаем в обратном порядке
	for (int i = allocation_count - 1; i >= 0; --i) {
		smu.deallocate(ptrs[i]);
	}

	auto end = std::clock();
	auto duration = std::chrono::microseconds(static_cast<long long>(
		(static_cast<double>(end - start) / CLOCKS_PER_SEC) * 1000000.0));

	std::cout << "\n--- EMBEDDED-STYLE STRESS TEST ---" << std::endl;
	std::cout << "Allocations: " << allocation_count << " (out of 100 attempted)" << std::endl;
	std::cout << "Time: " << duration.count() << " μs" << std::endl;
	std::cout << "Pool efficiency: " << (smu.freeBytes() * 100.0 / POOL_SIZE) << "% free after cleanup" << std::endl;

	SmuAllocContext::current_smu = nullptr;
	EXPECT_EQ(smu.busyNodes(), 0);
	EXPECT_TRUE(smu.checkIntegrity());
}

TEST(SmuJsonTest, MetadataExhaustion)
{
	const size_t POOL_SIZE = 128 * 1024; // Маленький пул
	alignas(16) static std::byte static_pool[POOL_SIZE];
	Smu smu(8, 32, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	SmuRapidAllocator allocator;
	SmuDocument d(&allocator);
	d.SetArray();

	bool caught_oom = false;
	try {
		for (int i = 0; i < 1000; ++i) {
			SmuValue v(i);
			d.PushBack(v, d.GetAllocator());
		}
	} catch (...) {
		caught_oom = true;
	}

	std::cout << "Final Nodes: " << smu.busyNodes() << std::endl;
	EXPECT_TRUE(smu.checkIntegrity());
}

TEST(SmuJsonTest, TheElasticStringMutation)
{
	const size_t POOL_SIZE = 128 * 1024;
	alignas(16) static std::byte static_pool[POOL_SIZE];
	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	{
		SmuRapidAllocator allocator;
		SmuDocument d(&allocator);
		d.SetObject();

		for (int i = 0; i < 50; ++i) {
			char key[16];
			std::sprintf(key, "fence_%02d", i);
			d.AddMember(SmuValue(key, d.GetAllocator()).Move(),
				    SmuValue(i).Move(), d.GetAllocator());
		}

		d.AddMember("elastic", "start", d.GetAllocator());

		std::string growing_str = "start";
		for (int i = 0; i < 100; ++i) {
			growing_str += "---some-more-data---";
			d["elastic"].SetString(growing_str.c_str(),
					       static_cast<rapidjson::SizeType>(
						       growing_str.length()),
					       d.GetAllocator());
		}

		std::cout
			<< "Final Elastic String Size: " << growing_str.length()
			<< std::endl;
		std::cout << "SMU Busy Bytes: " << smu.busyBytes() << std::endl;

		EXPECT_TRUE(smu.checkIntegrity());
	}

	EXPECT_EQ(smu.busyNodes(), 0);
}

TEST(SmuJsonTest, TheRecursiveVoid)
{
	const size_t POOL_SIZE = 256 * 1024;
	alignas(16) static std::byte static_pool[POOL_SIZE];
	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	{
		SmuRapidAllocator allocator;
		SmuDocument d(&allocator);
		d.SetObject();

		trap_enabled = true;

		SmuValue *current_level = &d;
		for (int i = 0; i < 256; ++i) {
			SmuValue next_level(rapidjson::kObjectType);

			SmuValue junk("junk_data_to_fragment_memory",
				      d.GetAllocator());
			next_level.AddMember("junk", junk, d.GetAllocator());
			next_level.RemoveMember("junk");

			current_level->AddMember("n", next_level,
						 d.GetAllocator());
			current_level = &((*current_level)["n"]);
		}

		std::cout << "Depth reached: 256" << std::endl;
		std::cout << "Busy before destruction: " << smu.busyBytes()
			  << std::endl;

		trap_enabled = false;
	}

	std::cout << "Busy after destruction: " << smu.busyNodes() << std::endl;
	EXPECT_EQ(smu.busyNodes(), 0);
	EXPECT_TRUE(smu.checkIntegrity());
}

TEST(SmuJsonTest, TheJsonChaosMonkey)
{
	const size_t POOL_SIZE = 256 * 1024;
	alignas(16) static std::byte static_pool[POOL_SIZE];
	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	{
		SmuRapidAllocator allocator;
		std::vector<SmuDocument> docs;
		docs.reserve(20);
		for (int i = 0; i < 20; ++i) {
			docs.emplace_back(&allocator);
		}

		for (int i = 0; i < 500; ++i) {
			int idx = std::rand() % 20;
			int action = std::rand() % 3;

			if (action == 0) {
				docs[idx].SetObject();
				docs[idx].AddMember("payload", "initial_data",
						    docs[idx].GetAllocator());
			} else if (action == 1) {
				if (!docs[idx].IsObject())
					docs[idx].SetObject();

				char key_buf[32];
				std::snprintf(key_buf, sizeof(key_buf), "k_%d",
					      i);

				SmuValue key(key_buf, docs[idx].GetAllocator());
				docs[idx].AddMember(key, SmuValue(i).Move(),
						    docs[idx].GetAllocator());
			} else {
				docs[idx].SetArray();
				int count = std::rand() % 10 + 1;
				for (int j = 0; j < count; ++j) {
					docs[idx].PushBack(
						j, docs[idx].GetAllocator());
				}
			}
		}

		std::cout << "Chaos finished. Busy bytes: " << smu.busyBytes()
			  << std::endl;
		EXPECT_TRUE(smu.checkIntegrity());
	}

	EXPECT_EQ(smu.busyNodes(), 0);
	SmuAllocContext::current_smu = nullptr;
}

#include <chrono>

template <typename AllocatorType>
void RunChaosBenchmark(const std::string &name,
		       AllocatorType *rapid_alloc = nullptr)
{
	auto start = std::clock();

	using DocType =
		rapidjson::GenericDocument<rapidjson::UTF8<>, AllocatorType>;
	using ValueType =
		rapidjson::GenericValue<rapidjson::UTF8<>, AllocatorType>;

	std::vector<DocType> docs;
	docs.reserve(20);
	for (int i = 0; i < 20; ++i) {
		if constexpr (std::is_same_v<AllocatorType,
					     rapidjson::CrtAllocator>)
			docs.emplace_back();
		else
			docs.emplace_back(rapid_alloc);
	}

	for (int i = 0; i < 100000; ++i) {
		int idx = std::rand() % 20;
		int action = std::rand() % 3;

		if (action == 0) {
			docs[idx].SetObject();
			docs[idx].AddMember("payload", "bench",
					    docs[idx].GetAllocator());
		} else if (action == 1) {
			if (!docs[idx].IsObject())
				docs[idx].SetObject();
			docs[idx].AddMember("key", i, docs[idx].GetAllocator());
		} else {
			docs[idx].SetArray();
			docs[idx].PushBack(i, docs[idx].GetAllocator());
		}
	}

	auto end = std::clock();
	std::chrono::duration<double, std::milli> duration = std::chrono::milliseconds(static_cast<long long>(
		(static_cast<double>(end - start) / CLOCKS_PER_SEC) * 1000.0));
	std::cout << "[" << name << "] Time: " << duration.count() << " ms"
		  << std::endl;
}

TEST(SmuJsonTest, BenchmarkVsMalloc)
{
	const size_t BIG_POOL = 10 * 1024 * 1024;
	std::byte *static_pool = new std::byte[BIG_POOL];

	Smu smu(16, 64, std::span<std::byte>(static_pool, BIG_POOL));
	SmuAllocContext::current_smu = &smu;
	SmuRapidAllocator smu_alloc;

	std::cout << "\n--- BENCHMARK REPORT ---" << std::endl;

	RunChaosBenchmark<SmuRapidAllocator>("SMU Allocator", &smu_alloc);

	RunChaosBenchmark<rapidjson::CrtAllocator>("System Malloc");

	delete[] static_pool;
	SmuAllocContext::current_smu = nullptr;
}

TEST(SmuJsonTest, TheFragmentationSurvival)
{
	const size_t POOL_SIZE = 256 * 1024; // 256 KB
	alignas(16) static std::byte static_pool[POOL_SIZE];
	Smu smu(16, 64, std::span<std::byte>(static_pool, POOL_SIZE));
	SmuAllocContext::current_smu = &smu;

	SmuRapidAllocator allocator;
	std::vector<SmuDocument *> docs;

	for (int i = 0; i < 100; ++i) {
		auto d = new SmuDocument(&allocator);
		d->SetObject();
		d->AddMember("data", SmuValue(i).Move(), d->GetAllocator());
		docs.push_back(d);
	}

	for (size_t i = 0; i < docs.size(); i += 2) {
		delete docs[i];
		docs[i] = nullptr;
	}

	bool success = true;
	try {
		for (int i = 0; i < 50; ++i) {
			if (docs[i] == nullptr) {
				docs[i] = new SmuDocument(&allocator);
				docs[i]->SetArray();
				for (int j = 0; j < 10; ++j) {
					docs[i]->PushBack(
						"short_str",
						docs[i]->GetAllocator());
				}
			}
		}
	} catch (...) {
		success = false;
	}

	std::cout << "Fragmentation Test - SMU Integrity: "
		  << (smu.checkIntegrity() ? "OK" : "FAIL") << std::endl;
	std::cout << "Memory after chessboard: " << smu.busyBytes() << " bytes"
		  << std::endl;

	for (auto d : docs)
		if (d)
			delete d;

	EXPECT_TRUE(success);
	EXPECT_EQ(smu.busyNodes(), 0);
	EXPECT_TRUE(smu.checkIntegrity());
}