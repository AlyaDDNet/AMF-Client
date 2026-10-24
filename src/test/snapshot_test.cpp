#include <base/mem.h>
#include <base/process.h>

#include <engine/shared/snapshot.h>

#include <generated/protocol.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(CONF_SNAPSHOT_FORENSICS)
namespace
{
class CForensicsLog
{
	std::filesystem::path m_Path;

public:
	explicit CForensicsLog(const char *pTestName)
	{
		m_Path = std::filesystem::temp_directory_path() / (std::string("amf_snapshot_forensics_") + std::to_string(process_id()) + "_" + pTestName + ".log");
		std::error_code Error;
		std::filesystem::remove(m_Path, Error);
	}
	~CForensicsLog()
	{
		std::error_code Error;
		std::filesystem::remove(m_Path, Error);
	}
	bool Attach(CSnapshotStorage &Storage) const { return Storage.DebugSetForensicsLogPath(m_Path.string().c_str()); }
	std::string Read() const
	{
		std::ifstream File(m_Path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
	}
};

int BuildForensicSnapshot(CSnapshotBuffer &Buffer)
{
	CSnapshotBuilder Builder;
	Builder.Init();
	for(int Id = 0; Id < 30; ++Id)
	{
		CNetObj_Flag Flag;
		Flag.m_X = Id;
		Flag.m_Y = 10;
		Flag.m_Team = 0;
		if(!Builder.NewItem(NETOBJTYPE_FLAG, Id, &Flag, sizeof(Flag)))
			return -1;
	}
	return Builder.Finish(&Buffer);
}
}

TEST(SnapshotStorageForensics, DetectsOffsets20And21WithoutRepair)
{
	CSnapshotBuffer Buffer;
	const int Size = BuildForensicSnapshot(Buffer);
	ASSERT_GT(Size, 96);
	CForensicsLog ForensicsLog(__func__);
	CSnapshotStorage Storage;
	ASSERT_TRUE(ForensicsLog.Attach(Storage));
	Storage.DebugSetContext(0, 1, 3, false);
	Storage.Add(100, 0, Size, Buffer.AsSnapshot(), Size, Buffer.AsSnapshot());
	ASSERT_TRUE(Storage.DebugCheck("before_test_write", 100, 1));
	const uint64_t Generation = Storage.DebugAllocationGeneration(Storage.m_pFirst);
	ASSERT_NE(Generation, 0u);
	mem_zero((unsigned char *)Storage.m_pFirst->m_pSnap + 88, 8);
	EXPECT_FALSE(Storage.DebugCheck("after_test_write", 101, 2));
	const auto Mutation = Storage.DebugLastMutation();
	ASSERT_TRUE(Mutation.has_value());
	EXPECT_EQ(Mutation->m_FirstByte, 88u);
	EXPECT_EQ(Mutation->m_DifferingBytes, 4u); // unchanged zero high bytes are NOT counted as changed bytes
	EXPECT_EQ(Mutation->m_Ranges, 2u);
	EXPECT_EQ(Mutation->m_Generation, Generation);
	EXPECT_STREQ(Mutation->m_pLastGood, "before_test_write");
	EXPECT_STREQ(Mutation->m_pFirstBad, "after_test_write");
	EXPECT_FALSE(Storage.DebugCheck("second_check_no_duplicate_report", 101, 2));
	EXPECT_FALSE(Storage.m_pFirst->m_pSnap->IsValid(Size));
	EXPECT_TRUE(Storage.m_pFirst->m_pAltSnap->IsValid(Size));
	EXPECT_TRUE(Buffer.AsSnapshot()->IsValid(Size));
	const CSnapshot *pRaw = nullptr;
	EXPECT_EQ(Storage.Get(100, nullptr, &pRaw, nullptr), Size);
	EXPECT_EQ(pRaw, Storage.m_pFirst->m_pSnap); // shadow never substitutes for native storage
	EXPECT_EQ(*(const int32_t *)((const unsigned char *)pRaw + 88), 0);
	EXPECT_EQ(*(const int32_t *)((const unsigned char *)pRaw + 92), 0);
}

TEST(SnapshotStorageForensics, RetiredAllocationIsNotReadAndGenerationIncreases)
{
	CSnapshotBuffer Buffer;
	const int Size = BuildForensicSnapshot(Buffer);
	ASSERT_GT(Size, 0);
	CForensicsLog ForensicsLog(__func__);
	CSnapshotStorage Storage;
	ASSERT_TRUE(ForensicsLog.Attach(Storage));
	Storage.DebugSetContext(1, 1, 3, false);
	Storage.Add(1, 0, Size, Buffer.AsSnapshot(), Size, Buffer.AsSnapshot());
	const uint64_t Generation = Storage.DebugAllocationGeneration(Storage.m_pFirst);
	Storage.PurgeUntil(2);
	EXPECT_NE(ForensicsLog.Read().find("reason=purge_until_free"), std::string::npos);
	EXPECT_EQ(Storage.DebugShadowCount(), 0u);
	EXPECT_TRUE(Storage.DebugCheck("after_free", 2, 2));
	Storage.Add(2, 0, Size, Buffer.AsSnapshot(), Size, Buffer.AsSnapshot());
	EXPECT_GT(Storage.DebugAllocationGeneration(Storage.m_pFirst), Generation);
	EXPECT_TRUE(Storage.DebugCheck("after_new_generation", 2, 2));
	Storage.PurgeAll();
	EXPECT_TRUE(Storage.DebugCheck("after_purge_all", 3, 3));
}

TEST(SnapshotStorageForensics, ShadowRingBoundDoesNotEvictNativeSnapshots)
{
	CSnapshotBuffer Buffer;
	const int Size = BuildForensicSnapshot(Buffer);
	ASSERT_GT(Size, 0);
	CForensicsLog ForensicsLog(__func__);
	CSnapshotStorage Storage;
	ASSERT_TRUE(ForensicsLog.Attach(Storage));
	for(int Tick = 1; Tick <= 24; ++Tick)
	{
		Storage.DebugSetContext(0, Tick, 3, false);
		Storage.Add(Tick, 0, Size, Buffer.AsSnapshot(), 0, nullptr);
	}
	EXPECT_EQ(Storage.DebugShadowCount(), 16u);
	EXPECT_EQ(Storage.Get(1, nullptr, nullptr, nullptr), Size);
	EXPECT_EQ(Storage.m_pFirst->m_Tick, 1);
	EXPECT_EQ(Storage.m_pLast->m_Tick, 24);
	EXPECT_TRUE(Storage.DebugCheck("bounded_ring", 24, 24));
}

TEST(SnapshotStorageForensics, FrameCheckIsBoundedPerSnapshotSequence)
{
	CSnapshotBuffer Buffer;
	const int Size = BuildForensicSnapshot(Buffer);
	ASSERT_GT(Size, 0);
	CForensicsLog ForensicsLog(__func__);
	CSnapshotStorage Storage;
	ASSERT_TRUE(ForensicsLog.Attach(Storage));
	Storage.DebugSetContext(0, 1, 3, false);
	Storage.Add(1, 0, Size, Buffer.AsSnapshot(), 0, nullptr);
	ASSERT_TRUE(Storage.DebugCheck("frame_start", 1, 1, 0));
	mem_zero((unsigned char *)Storage.m_pFirst->m_pSnap + 88, 8);
	EXPECT_TRUE(Storage.DebugCheck("frame_start", 1, 1, 0)); // deliberately not a continuous per-frame scan
	EXPECT_FALSE(Storage.DebugCheck("next_network_sequence", 2, 2, 0));
}

TEST(SnapshotStorageForensics, PersistsInsertionMutationAndInvalidBaseBeforeUnpack)
{
	CSnapshotBuffer Buffer;
	const int Size = BuildForensicSnapshot(Buffer);
	ASSERT_GT(Size, 96);
	CForensicsLog ForensicsLog(__func__);
	CSnapshotStorage Storage;
	ASSERT_TRUE(ForensicsLog.Attach(Storage));
	Storage.DebugSetContext(0, 17, 3, false);
	Storage.Add(100, 0, Size, Buffer.AsSnapshot(), Size, Buffer.AsSnapshot());
	const uint64_t Generation = Storage.DebugAllocationGeneration(Storage.m_pFirst);
	ASSERT_NE(Generation, 0u);
	std::string Log = ForensicsLog.Read();
	EXPECT_NE(Log.find("reason=snapshot_added"), std::string::npos);
	EXPECT_NE(Log.find("stored_valid=1"), std::string::npos);
	EXPECT_NE(Log.find("holder_gen="), std::string::npos);
	EXPECT_NE(Log.find("raw_gen="), std::string::npos);
	EXPECT_NE(Log.find("alt_gen="), std::string::npos);
	EXPECT_NE(Log.find("stored_fingerprint="), std::string::npos);
	EXPECT_NE(Log.find("lifecycle timestamp="), std::string::npos);

	ASSERT_TRUE(Storage.DebugCheck("before_test_write", 100, 17));
	mem_zero((unsigned char *)Storage.m_pFirst->m_pSnap + 88, 8);
	EXPECT_FALSE(Storage.DebugCheck("after_test_write", 101, 18));
	Log = ForensicsLog.Read();
	EXPECT_NE(Log.find("snapshot_shadow: MUTATION"), std::string::npos);
	EXPECT_NE(Log.find("snapshot_shadow: INTERVAL last_good=before_test_write"), std::string::npos);
	EXPECT_NE(Log.find("changed_range=[88,"), std::string::npos);
	EXPECT_NE(Log.find("offsets20_21 address="), std::string::npos);

	const CSnapshot *pBase = nullptr;
	ASSERT_EQ(Storage.Get(100, nullptr, &pBase, nullptr), Size);
	ASSERT_FALSE(pBase->IsValid((size_t)Size));
	ASSERT_TRUE(Storage.DebugPersistInvalidBase(102, 100, 19, false, false, pBase, Size));
	Log = ForensicsLog.Read();
	EXPECT_NE(Log.find("INVALID_BASE timestamp_epoch="), std::string::npos);
	EXPECT_NE(Log.find("current_tick=102 base_tick=100 sequence=19 sixup=0 dummy=0"), std::string::npos);
	EXPECT_NE(Log.find("base_shadow_comparison=MUTATION"), std::string::npos);
	EXPECT_NE(Log.find("base_allocation_size_matches=1"), std::string::npos);
	EXPECT_NE(Log.find("holder_gen="), std::string::npos);
	EXPECT_NE(Log.find("raw_gen="), std::string::npos);
	EXPECT_NE(Log.find("alt_gen="), std::string::npos);
	EXPECT_NE(Log.find("reason=add"), std::string::npos);
	EXPECT_NE(Log.find("target_lifecycle"), std::string::npos);
	EXPECT_NE(Log.find("timestamp="), std::string::npos);
}
#endif

TEST(Snapshot, CrcOneInt)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 4;
	Flag.m_Y = 0;
	Flag.m_Team = 0;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 4);
}

TEST(Snapshot, CrcTwoInts)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 1;
	Flag.m_Y = 1;
	Flag.m_Team = 0;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 2);
}

TEST(Snapshot, CrcBiggerInts)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 99999999;
	Flag.m_Y = 1;
	Flag.m_Team = 1;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 100000001);
}

TEST(Snapshot, CrcOverflow)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 0xFFFFFFFF;
	Flag.m_Y = 1;
	Flag.m_Team = 1;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 1);
}

TEST(Snapshot, StorageGet)
{
	CSnapshotStorage Storage;

	// `CSnapshotStorage` needs snapshots in increasing tick order.
	const char aData[8] = {0};
	Storage.Add(10, 1000, 1, aData, 0, nullptr);
	Storage.Add(20, 2000, 2, aData, 0, nullptr);
	Storage.Add(30, 3000, 3, aData, 0, nullptr);
	Storage.Add(40, 4000, 4, aData, 0, nullptr);

	int64_t Tagtime = -1;

	// Retrieve existing snapshots.
	EXPECT_EQ(Storage.Get(40, &Tagtime, nullptr, nullptr), 4);
	EXPECT_EQ(Tagtime, 4000);
	EXPECT_EQ(Storage.Get(10, &Tagtime, nullptr, nullptr), 1);
	EXPECT_EQ(Tagtime, 1000);
	EXPECT_EQ(Storage.Get(30, &Tagtime, nullptr, nullptr), 3);
	EXPECT_EQ(Tagtime, 3000);

	// Check non-existing snapshots in before, within and after the range.
	EXPECT_EQ(Storage.Get(50, nullptr, nullptr, nullptr), -1);
	EXPECT_EQ(Storage.Get(5, nullptr, nullptr, nullptr), -1);
	EXPECT_EQ(Storage.Get(25, nullptr, nullptr, nullptr), -1);
}
