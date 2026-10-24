/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "snapshot.h"

#include "compression.h"
#include "uuid_manager.h"

#include <base/bytes.h>
#include <base/dbg.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/process.h>
#include <base/str.h>
#include <base/time.h>

#include <generated/protocol7.h>
#include <generated/protocolglue.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#if defined(CONF_SNAPSHOT_FORENSICS)
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
#endif

// CSnapshot

const CSnapshotItem *CSnapshot::GetItem(int Index) const
{
	return (const CSnapshotItem *)(DataStart() + Offsets()[Index]);
}

const CSnapshot CSnapshot::ms_EmptySnapshot;

int CSnapshot::GetItemSize(int Index) const
{
	if(Index == m_NumItems - 1)
		return (m_DataSize - Offsets()[Index]) - sizeof(CSnapshotItem);
	return (Offsets()[Index + 1] - Offsets()[Index]) - sizeof(CSnapshotItem);
}

int CSnapshot::GetItemType(int Index) const
{
	int InternalType = GetItem(Index)->InternalType();
	return GetExternalItemType(InternalType);
}

int CSnapshot::GetExternalItemType(int InternalType) const
{
	if(InternalType < OFFSET_UUID_TYPE)
	{
		return InternalType;
	}

	int TypeItemIndex = GetItemIndex(InternalType); // NETOBJTYPE_EX
	if(TypeItemIndex == -1 || GetItemSize(TypeItemIndex) < (int)sizeof(CUuid))
	{
		return -1;
	}
	const CSnapshotItem *pTypeItem = GetItem(TypeItemIndex);
	CUuid Uuid;
	for(size_t i = 0; i < sizeof(CUuid) / sizeof(int32_t); i++)
		uint_to_bytes_be(&Uuid.m_aData[i * sizeof(int32_t)], pTypeItem->Data()[i]);

	return g_UuidManager.LookupUuid(Uuid);
}

int CSnapshot::GetItemIndex(int Key) const
{
	// TODO: OPT: this should not be a linear search. very bad
	for(int i = 0; i < m_NumItems; i++)
	{
		if(GetItem(i)->Key() == Key)
			return i;
	}
	return -1;
}

void CSnapshot::InvalidateItem(int Index)
{
	((CSnapshotItem *)(DataStart() + Offsets()[Index]))->Invalidate();
}

const void *CSnapshot::FindItem(int Type, int Id) const
{
	int InternalType = Type;
	if(Type >= OFFSET_UUID)
	{
		CUuid TypeUuid = g_UuidManager.GetUuid(Type);
		int aTypeUuidItem[sizeof(CUuid) / sizeof(int32_t)];
		for(size_t i = 0; i < sizeof(CUuid) / sizeof(int32_t); i++)
			aTypeUuidItem[i] = bytes_be_to_uint(&TypeUuid.m_aData[i * sizeof(int32_t)]);

		bool Found = false;
		for(int i = 0; i < m_NumItems; i++)
		{
			const CSnapshotItem *pItem = GetItem(i);
			if(pItem->InternalType() == 0 && pItem->Id() >= OFFSET_UUID_TYPE) // NETOBJTYPE_EX
			{
				if(mem_comp(pItem->Data(), aTypeUuidItem, sizeof(CUuid)) == 0)
				{
					InternalType = pItem->Id();
					Found = true;
					break;
				}
			}
		}
		if(!Found)
		{
			return nullptr;
		}
	}
	int Index = GetItemIndex((InternalType << 16) | Id);
	return Index < 0 ? nullptr : GetItem(Index)->Data();
}

unsigned CSnapshot::Crc() const
{
	unsigned int Crc = 0;

	for(int i = 0; i < m_NumItems; i++)
	{
		const CSnapshotItem *pItem = GetItem(i);
		int Size = GetItemSize(i);

		for(size_t b = 0; b < Size / sizeof(int32_t); b++)
			Crc += pItem->Data()[b];
	}
	return Crc;
}

void CSnapshot::DebugDump() const
{
	dbg_msg("snapshot", "data_size=%d num_items=%d", m_DataSize, m_NumItems);
	for(int i = 0; i < m_NumItems; i++)
	{
		const CSnapshotItem *pItem = GetItem(i);
		int Size = GetItemSize(i);
		dbg_msg("snapshot", "\ttype=%d id=%d", pItem->InternalType(), pItem->Id());
		for(size_t b = 0; b < Size / sizeof(int32_t); b++)
			dbg_msg("snapshot", "\t\t%3d %12d\t%08x", (int)b, pItem->Data()[b], pItem->Data()[b]);
	}
}

void CSnapshot::DebugDumpInvalidLayout(size_t ActualSize) const
{
	if(ActualSize < sizeof(CSnapshot) || ActualSize > MAX_SIZE ||
		m_NumItems < 0 || m_NumItems > MAX_ITEMS || m_DataSize < 0 || m_DataSize > MAX_SIZE)
		return;
	const size_t ExpectedSize = sizeof(CSnapshot) + sizeof(int) * (size_t)m_NumItems + (size_t)m_DataSize;
	if(ExpectedSize != ActualSize)
		return;

	for(int i = 0; i < m_NumItems; i++)
	{
		const int Offset = Offsets()[i];
		const int NextOffset = i + 1 < m_NumItems ? Offsets()[i + 1] : m_DataSize;
		const bool OffsetInRange = Offset >= 0 && Offset <= m_DataSize && Offset % sizeof(int32_t) == 0;
		const bool NextOffsetInRange = NextOffset >= 0 && NextOffset <= m_DataSize && NextOffset % sizeof(int32_t) == 0;
		const bool ItemHeaderReadable = OffsetInRange && Offset <= m_DataSize - (int)sizeof(CSnapshotItem);
		const int ItemSize = OffsetInRange && NextOffsetInRange ? NextOffset - Offset - (int)sizeof(CSnapshotItem) : -1;
		if(!OffsetInRange || !NextOffsetInRange || !ItemHeaderReadable || ItemSize < 0 || ItemSize % sizeof(int32_t) != 0)
		{
			const CSnapshotItem *pItem = ItemHeaderReadable ? GetItem(i) : nullptr;
			dbg_msg("snapshot-corruption", "invalid stored base item detail: index=%d type=%d id=%d offset=%d next_offset=%d size=%d data=%d actual=%" PRIzu,
				i,
				pItem ? pItem->InternalType() : -1,
				pItem ? pItem->Id() : -1,
				Offset,
				NextOffset,
				ItemSize,
				m_DataSize,
				ActualSize);
		}
	}
}

bool CSnapshot::IsValid(size_t ActualSize) const
{
	// validate total size
	if(ActualSize < sizeof(CSnapshot) ||
		ActualSize > MAX_SIZE ||
		m_NumItems < 0 ||
		m_NumItems > MAX_ITEMS ||
		m_DataSize < 0 ||
		ActualSize != TotalSize())
	{
		return false;
	}

	// validate item offsets
	const int *pOffsets = Offsets();
	for(int Index = 0; Index < m_NumItems; Index++)
	{
		if(pOffsets[Index] < 0 ||
			pOffsets[Index] > m_DataSize ||
			pOffsets[Index] % sizeof(int32_t) != 0)
		{
			return false;
		}
	}

	// validate item sizes
	for(int Index = 0; Index < m_NumItems; Index++)
	{
		const int ItemSize = GetItemSize(Index); // the offsets must be validated before using this
		if(ItemSize < 0 ||
			ItemSize % sizeof(int32_t) != 0)
		{
			return false;
		}
	}

	return true;
}

// CSnapshotDelta

enum
{
	HASHLIST_SIZE = 256,
	HASHLIST_BUCKET_SIZE = 64,
};

struct CItemList
{
	int m_Num;
	int m_aKeys[HASHLIST_BUCKET_SIZE];
	int m_aIndex[HASHLIST_BUCKET_SIZE];
};

static inline size_t CalcHashId(int Key)
{
	// djb2 (http://www.cse.yorku.ca/~oz/hash.html)
	unsigned Hash = 5381;
	for(unsigned Shift = 0; Shift < sizeof(int); Shift++)
		Hash = ((Hash << 5) + Hash) + ((Key >> (Shift * 8)) & 0xFF);
	return Hash % HASHLIST_SIZE;
}

static void GenerateHash(CItemList *pHashlist, const CSnapshot *pSnapshot)
{
	for(int i = 0; i < HASHLIST_SIZE; i++)
		pHashlist[i].m_Num = 0;

	for(int i = 0; i < pSnapshot->NumItems(); i++)
	{
		int Key = pSnapshot->GetItem(i)->Key();
		size_t HashId = CalcHashId(Key);
		if(pHashlist[HashId].m_Num < HASHLIST_BUCKET_SIZE)
		{
			pHashlist[HashId].m_aIndex[pHashlist[HashId].m_Num] = i;
			pHashlist[HashId].m_aKeys[pHashlist[HashId].m_Num] = Key;
			pHashlist[HashId].m_Num++;
		}
	}
}

static int GetItemIndexHashed(int Key, const CItemList *pHashlist)
{
	size_t HashId = CalcHashId(Key);
	for(int i = 0; i < pHashlist[HashId].m_Num; i++)
	{
		if(pHashlist[HashId].m_aKeys[i] == Key)
			return pHashlist[HashId].m_aIndex[i];
	}

	return -1;
}

int CSnapshotDelta::DiffItem(const int *pPast, const int *pCurrent, int *pOut, int Size)
{
	int Needed = 0;
	while(Size)
	{
		// subtraction with wrapping by casting to unsigned
		*pOut = (unsigned)*pCurrent - (unsigned)*pPast;
		Needed |= *pOut;
		pOut++;
		pPast++;
		pCurrent++;
		Size--;
	}

	return Needed;
}

void CSnapshotDelta::UndiffItem(const int *pPast, const int *pDiff, int *pOut, int Size, uint64_t *pDataRate)
{
	while(Size)
	{
		// addition with wrapping by casting to unsigned
		*pOut = (unsigned)*pPast + (unsigned)*pDiff;

		if(*pDiff == 0)
			*pDataRate += 1;
		else
		{
			unsigned char aBuf[CVariableInt::MAX_BYTES_PACKED];
			unsigned char *pEnd = CVariableInt::Pack(aBuf, *pDiff, sizeof(aBuf));
			*pDataRate += (uint64_t)(pEnd - (unsigned char *)aBuf) * 8;
		}

		pOut++;
		pPast++;
		pDiff++;
		Size--;
	}
}

CSnapshotDelta::CSnapshotDelta()
{
	std::fill(std::begin(m_aItemSizes), std::end(m_aItemSizes), 0);
	std::fill(std::begin(m_aSnapshotDataRate), std::end(m_aSnapshotDataRate), 0);
	std::fill(std::begin(m_aSnapshotDataUpdates), std::end(m_aSnapshotDataUpdates), 0);
	mem_zero(&m_Empty, sizeof(m_Empty));
}

CSnapshotDelta::CSnapshotDelta(const CSnapshotDelta &Old)
{
	mem_copy(m_aItemSizes, Old.m_aItemSizes, sizeof(m_aItemSizes));
	mem_copy(m_aSnapshotDataRate, Old.m_aSnapshotDataRate, sizeof(m_aSnapshotDataRate));
	mem_copy(m_aSnapshotDataUpdates, Old.m_aSnapshotDataUpdates, sizeof(m_aSnapshotDataUpdates));
	mem_zero(&m_Empty, sizeof(m_Empty));
}

void CSnapshotDelta::SetStaticsize(int ItemType, size_t Size)
{
	dbg_assert(ItemType >= 0 && ItemType < MAX_NETOBJSIZES, "ItemType invalid");
	dbg_assert(Size <= (size_t)std::numeric_limits<int16_t>::max(), "Size invalid");
	m_aItemSizes[ItemType] = Size;
}

const CSnapshotDelta::CData *CSnapshotDelta::EmptyDelta() const
{
	return &m_Empty;
}

// TODO: OPT: this should be made much faster
int CSnapshotDelta::CreateDelta(const CSnapshot *pFrom, const CSnapshot *pTo, CSnapshotDeltaBuffer *pDstData)
{
	CData *pDelta = (CData *)pDstData->m_aData;
	int *pData = (int *)pDelta->m_aData;

	pDelta->m_NumDeletedItems = 0;
	pDelta->m_NumUpdateItems = 0;
	pDelta->m_NumTempItems = 0;

	CItemList aHashlist[HASHLIST_SIZE];
	GenerateHash(aHashlist, pTo);

	// pack deleted stuff
	for(int i = 0; i < pFrom->NumItems(); i++)
	{
		const CSnapshotItem *pFromItem = pFrom->GetItem(i);
		if(GetItemIndexHashed(pFromItem->Key(), aHashlist) == -1)
		{
			// deleted
			pDelta->m_NumDeletedItems++;
			*pData = pFromItem->Key();
			pData++;
		}
	}

	GenerateHash(aHashlist, pFrom);

	// fetch previous indices
	// we do this as a separate pass because it helps the cache
	int aPastIndices[CSnapshot::MAX_ITEMS];
	const int NumItems = pTo->NumItems();
	for(int i = 0; i < NumItems; i++)
	{
		const CSnapshotItem *pCurItem = pTo->GetItem(i); // O(1) .. O(n)
		aPastIndices[i] = GetItemIndexHashed(pCurItem->Key(), aHashlist); // O(n) .. O(n^n)
	}

	for(int i = 0; i < NumItems; i++)
	{
		// do delta
		const int ItemSize = pTo->GetItemSize(i); // O(1) .. O(n)
		const CSnapshotItem *pCurItem = pTo->GetItem(i); // O(1) .. O(n)
		const int PastIndex = aPastIndices[i];
		const bool IncludeSize = pCurItem->InternalType() >= MAX_NETOBJSIZES || !m_aItemSizes[pCurItem->InternalType()];

		if(PastIndex != -1)
		{
			int *pItemDataDst = pData + 3;

			const CSnapshotItem *pPastItem = pFrom->GetItem(PastIndex);

			if(!IncludeSize)
				pItemDataDst = pData + 2;

			if(DiffItem(pPastItem->Data(), pCurItem->Data(), pItemDataDst, ItemSize / sizeof(int32_t)))
			{
				*pData++ = pCurItem->InternalType();
				*pData++ = pCurItem->Id();
				if(IncludeSize)
					*pData++ = ItemSize / sizeof(int32_t);
				pData += ItemSize / sizeof(int32_t); // NOLINT(bugprone-sizeof-expression)
				pDelta->m_NumUpdateItems++;
			}
		}
		else
		{
			*pData++ = pCurItem->InternalType();
			*pData++ = pCurItem->Id();
			if(IncludeSize)
				*pData++ = ItemSize / sizeof(int32_t);

			mem_copy(pData, pCurItem->Data(), ItemSize);
			pData += ItemSize / sizeof(int32_t); // NOLINT(bugprone-sizeof-expression)
			pDelta->m_NumUpdateItems++;
		}
	}

	if(!pDelta->m_NumDeletedItems && !pDelta->m_NumUpdateItems && !pDelta->m_NumTempItems)
		return 0;

	return (int)((char *)pData - (char *)pDelta);
}

int CSnapshotDelta::DebugDumpDelta(const void *pSrcData, int DataSize)
{
	CData *pDelta = (CData *)pSrcData;
	int *pData = (int *)pDelta->m_aData;
	int *pEnd = (int *)(((char *)pSrcData + DataSize));

	dbg_msg("delta_dump", "+-----------------------------------------------");
	if(DataSize < 3 * (int)sizeof(int32_t))
	{
		dbg_msg("delta_dump", "|  delta size %d too small. Should at least fit the empty delta header.", DataSize);
		return -505;
	}

	dbg_msg("delta_dump", "|  data_size=%d", DataSize);

	int DumpIndex = 0;

	// dump header
	{
		int *pDumpHeader = (int *)pSrcData;
		dbg_msg("delta_dump", "|  %3d %12d  %08x m_NumDeletedItems=%d", DumpIndex++, *pDumpHeader, *pDumpHeader, *pDumpHeader);
		pDumpHeader++;
		dbg_msg("delta_dump", "|  %3d %12d  %08x m_NumUpdatedItems=%d", DumpIndex++, *pDumpHeader, *pDumpHeader, *pDumpHeader);
		pDumpHeader++;
		dbg_msg("delta_dump", "|  %3d %12d  %08x _zero=%d", DumpIndex++, *pDumpHeader, *pDumpHeader, *pDumpHeader);
		pDumpHeader++;

		dbg_assert(pDumpHeader == pData, "invalid header size");
	}

	// unpack deleted stuff
	int *pDeleted = pData;
	if(pDelta->m_NumDeletedItems < 0)
	{
		dbg_msg("delta_dump", "|  Invalid delta. Number of deleted items %d is negative.", pDelta->m_NumDeletedItems);
		return -201;
	}
	if(pDelta->m_NumDeletedItems > pEnd - pData)
	{
		dbg_msg("delta_dump", "|  Invalid delta. Read past the end.");
		return -101;
	}
	pData += pDelta->m_NumDeletedItems;

	// list deleted items
	// (all other items should be copied from the last full snap)
	for(int d = 0; d < pDelta->m_NumDeletedItems; d++)
	{
		int Type = pDeleted[d] >> 16;
		int Id = pDeleted[d] & 0xffff;
		dbg_msg("delta_dump", "  %3d %12d %08x deleted Type=%d Id=%d", DumpIndex++, pDeleted[d], pDeleted[d], Type, Id);
	}

	// unpack updated stuff
	for(int i = 0; i < pDelta->m_NumUpdateItems; i++)
	{
		if(pData + 2 > pEnd)
		{
			dbg_msg("delta_dump", "|  Invalid delta. NumUpdateItems=%d can't be fit into DataSize=%d", pDelta->m_NumUpdateItems, DataSize);
			return -102;
		}

		dbg_msg("delta_dump", "|  --------------------------------");
		dbg_msg("delta_dump", "|  %3d %12d  %08x updated Type=%d", DumpIndex++, *pData, *pData, *pData);
		const int Type = *pData++;
		if(Type < 0 || Type > CSnapshot::MAX_TYPE)
		{
			dbg_msg("delta_dump", "|  Invalid delta. Type=%d out of range (0 - %d)", Type, CSnapshot::MAX_TYPE);
			return -202;
		}

		dbg_msg("delta_dump", "|  %3d %12d  %08x updated Id=%d", DumpIndex++, *pData, *pData, *pData);
		const int Id = *pData++;
		if(Id < 0 || Id > CSnapshot::MAX_ID)
		{
			dbg_msg("delta_dump", "|  Invalid delta. Id=%d out of range (0 - %d)", Id, CSnapshot::MAX_ID);
			return -203;
		}

		// size of the item in bytes
		int ItemSize;
		if(Type < MAX_NETOBJSIZES && m_aItemSizes[Type])
		{
			ItemSize = m_aItemSizes[Type];
			dbg_msg("delta_dump", "|                             updated size=%d (known)", ItemSize);
		}
		else
		{
			if(pData + 1 > pEnd)
			{
				dbg_msg("delta_dump", "|  Invalid delta. Expected item size but got end of data.");
				return -103;
			}
			if(*pData < 0 || (size_t)*pData > std::numeric_limits<int32_t>::max() / sizeof(int32_t))
			{
				dbg_msg("delta_dump", "|  Invalid delta. Item size %d out of range (0 - %" PRIzu ")", *pData, std::numeric_limits<int32_t>::max() / sizeof(int32_t));
				return -204;
			}
			dbg_msg("delta_dump", "|  %3d %12d  %08x updated size=%d", DumpIndex++, *pData, *pData, *pData);
			ItemSize = (*pData++) * sizeof(int32_t);
		}

		if(ItemSize < 0)
		{
			dbg_msg("delta_dump", "|  Invalid delta. Item size %d is negative.", ItemSize);
			return -205;
		}
		if((const char *)pEnd - (const char *)pData < ItemSize)
		{
			dbg_msg("delta_dump", "|  Invalid delta. Item with type=%d id=%d size=%d does not fit into the delta.", Type, Id, ItemSize);
			return -205;
		}

		// divide item size in bytes by size of integers
		// to get the number of integers we want to increment the pointer
		const int *pItemEnd = pData + (ItemSize / sizeof(int32_t));

		for(size_t b = 0; b < ItemSize / sizeof(int32_t); b++)
		{
			dbg_msg("delta_dump", "|  %3d %12d  %08x item data", DumpIndex++, *pData, *pData);
			pData++;
		}

		dbg_assert(pItemEnd == pData, "Incorrect amount of data dumped for this item.");
	}

	dbg_msg("delta_dump", "|  Finished with expected_data_size=%d parsed_data_size=%" PRIzu, DataSize, (pData - (int *)pSrcData) * sizeof(int32_t));
	dbg_msg("delta_dump", "+--------------------");

	return 0;
}

int CSnapshotDelta::UnpackDelta(const CSnapshot *pFrom, CSnapshotBuffer *pTo, const void *pSrcData, int DataSize, const CSnapshotDebugContext *pDebugContext)
{
	CData *pDelta = (CData *)pSrcData;
	int *pData = (int *)pDelta->m_aData;
	int *pEnd = (int *)(((char *)pSrcData + DataSize));

	CSnapshotBuilder Builder;
	Builder.Init();
	if(pDebugContext)
		Builder.SetDebugContext(*pDebugContext);
	else
		Builder.SetDebugContext({.m_pStage = "snapshot-delta-unpack"});
	if(pDebugContext != nullptr && pDebugContext->m_BaseSnapshotSize >= 0 &&
		!pFrom->IsValid(static_cast<size_t>(pDebugContext->m_BaseSnapshotSize)))
	{
		// A base snapshot is validated before it enters storage. If its metadata
		// changes before a later delta references it, fail at the ownership
		// boundary and preserve the native fatal policy instead of dereferencing
		// corrupt item offsets below.
		pFrom->DebugDumpInvalidLayout(static_cast<size_t>(pDebugContext->m_BaseSnapshotSize));
		dbg_msg("snapshot-corruption",
			"invalid stored delta base before item copy: tick=%d delta_tick=%d conn=%d seq=%d sixup=%d dummy=%d base=%p base_size=%d base_items=%d base_data=%d",
			pDebugContext->m_GameTick,
			pDebugContext->m_DeltaTick,
			pDebugContext->m_Connection,
			pDebugContext->m_Sequence,
			pDebugContext->m_Sixup,
			pDebugContext->m_Dummy,
			static_cast<const void *>(pFrom),
			pDebugContext->m_BaseSnapshotSize,
			pFrom->NumItems(),
			pFrom->DataSize());
		dbg_assert(false, "Invalid stored delta base snapshot before item copy");
	}

	// unpack deleted stuff
	int *pDeleted = pData;
	if(pDelta->m_NumDeletedItems < 0)
		return -201;
	if(pDelta->m_NumDeletedItems > pEnd - pData)
		return -101;
	pData += pDelta->m_NumDeletedItems;

	// copy all non deleted stuff
	for(int i = 0; i < pFrom->NumItems(); i++)
	{
		const CSnapshotItem *pFromItem = pFrom->GetItem(i);
		const int ItemSize = pFrom->GetItemSize(i);
		bool Keep = true;
		for(int d = 0; d < pDelta->m_NumDeletedItems; d++)
		{
			if(pDeleted[d] == pFromItem->Key())
			{
				Keep = false;
				break;
			}
		}

		if(Keep)
		{
			// keep it
			if(ItemSize < 0 || ItemSize % sizeof(int32_t) != 0)
			{
				dbg_msg("snapshot-corruption",
					"invalid retained base item: tick=%d delta_tick=%d conn=%d seq=%d sixup=%d dummy=%d base_index=%d base_items=%d base_data=%d type=%d id=%d size=%d",
					pDebugContext ? pDebugContext->m_GameTick : -1,
					pDebugContext ? pDebugContext->m_DeltaTick : -1,
					pDebugContext ? pDebugContext->m_Connection : -1,
					pDebugContext ? pDebugContext->m_Sequence : -1,
					pDebugContext ? pDebugContext->m_Sixup : false,
					pDebugContext ? pDebugContext->m_Dummy : false,
					i,
					pFrom->NumItems(),
					pFrom->DataSize(),
					pFromItem->InternalType(),
					pFromItem->Id(),
					ItemSize);
			}
			if(!Builder.NewItem(pFromItem->InternalType(), pFromItem->Id(), pFromItem->Data(), ItemSize))
			{
				return -301;
			}
		}
	}

	// unpack updated stuff
	for(int i = 0; i < pDelta->m_NumUpdateItems; i++)
	{
		if(pData + 2 > pEnd)
			return -102;

		const int Type = *pData++;
		if(Type < 0 || Type > CSnapshot::MAX_TYPE)
			return -202;

		const int Id = *pData++;
		if(Id < 0 || Id > CSnapshot::MAX_ID)
			return -203;

		int ItemSize;
		if(Type < MAX_NETOBJSIZES && m_aItemSizes[Type])
			ItemSize = m_aItemSizes[Type];
		else
		{
			if(pData + 1 > pEnd)
				return -103;
			if(*pData < 0 || (size_t)*pData > std::numeric_limits<int32_t>::max() / sizeof(int32_t))
				return -204;
			ItemSize = (*pData++) * sizeof(int32_t);
		}

		if(ItemSize < 0 || (const char *)pEnd - (const char *)pData < ItemSize)
			return -205;

		const int Key = (Type << 16) | Id;

		// create the item if needed
		std::optional<int> ExistingIndex = Builder.FindItemIndexByKey(Key);
		int *pNewData;
		if(ExistingIndex)
		{
			if(ItemSize != Builder.GetItemSize(ExistingIndex.value()))
			{
				return -206;
			}
			pNewData = Builder.GetItemData(ExistingIndex.value());
		}
		else
		{
			pNewData = (int *)Builder.NewItemRaw(Type, Id, ItemSize);
		}

		if(!pNewData)
			return -302;

		const int FromIndex = pFrom->GetItemIndex(Key);
		if(FromIndex != -1)
		{
			if(pFrom->GetItemSize(FromIndex) != ItemSize)
			{
				return -207;
			}
			// we got an update so we need to apply the diff
			UndiffItem(pFrom->GetItem(FromIndex)->Data(), pData, pNewData, ItemSize / sizeof(int32_t), &m_aSnapshotDataRate[Type]);
		}
		else // no previous, just copy the pData
		{
			mem_copy(pNewData, pData, ItemSize);
			m_aSnapshotDataRate[Type] += ItemSize * 8;
		}
		m_aSnapshotDataUpdates[Type]++;

		pData += ItemSize / sizeof(int32_t); // NOLINT(bugprone-sizeof-expression)
	}

	// finish up
	return Builder.Finish(pTo);
}

// CSnapshotStorage

#if defined(CONF_SNAPSHOT_FORENSICS)
namespace
{
static_assert(sizeof(CSnapshot) == 8 && sizeof(int32_t) == 4, "Update raw offset watch layout for this target");
std::atomic<uint64_t> s_SnapshotAllocationGeneration{0};
std::atomic<uint64_t> s_SnapshotLifetimeSequence{0};
CSnapshotStorage *s_pEntityLinkForensicsStorage = nullptr;

uint64_t ShadowFingerprint(const unsigned char *pData, size_t Size)
{
	uint64_t Hash = 14695981039346656037ULL;
	for(size_t i = 0; i < Size; ++i)
		Hash = (Hash ^ pData[i]) * 1099511628211ULL;
	return Hash;
}
}

// Stable named breakpoint target: stop here, then install an 8-byte hardware
// WRITE breakpoint at pOffsets20. It is called only once when explicitly armed
// via AMF_SNAPSHOT_WATCH_TICK (minimum tick) and/or AMF_SNAPSHOT_WATCH_MIN_ITEMS.
// The optional break executes only while a debugger is attached.
#if defined(_MSC_VER)
__declspec(noinline)
#endif
void SnapshotForensicsWatchpoint(const void *pHolder, const void *pOffsets20, uint64_t Generation)
{
	const char *pBreak = std::getenv("AMF_SNAPSHOT_WATCH_BREAK");
#if defined(_WIN32)
	if(pBreak && pBreak[0] == '1' && IsDebuggerPresent())
		DebugBreak();
#endif
	(void)pHolder;
	(void)pOffsets20;
	(void)Generation;
	(void)pBreak;
}

class CSnapshotStorage::CForensics
{
public:
	struct CLifetime
	{
		uintptr_t m_Holder = 0, m_Raw = 0, m_Alt = 0;
		size_t m_Size = 0, m_AltSize = 0;
		uint64_t m_HolderGeneration = 0, m_RawGeneration = 0, m_AltGeneration = 0, m_AddSequence = 0;
		int m_Tick = -1, m_Connection = -1, m_NetworkSequence = -1;
	};
	struct CEvent
	{
		CLifetime m_Lifetime;
		uint64_t m_Sequence = 0;
		const char *m_pReason = "unused";
		int64_t m_Timestamp = 0;
	};
	struct CShadow
	{
		CLifetime m_Lifetime;
		std::vector<unsigned char> m_Bytes;
		bool m_Alive = false, m_Reported = false;
		bool m_StoredValid = false;
		int m_NumItems = -1, m_DataSize = -1;
		const char *m_pLastGood = "after_add";
		uint64_t m_LastGoodCheck = 0;
		int m_LastGoodTick = -1, m_LastGoodSequence = -1;
		const char *m_pFirstBad = nullptr;
		uint64_t m_FirstBadCheck = 0;
		int m_FirstBadTick = -1, m_FirstBadSequence = -1;
		int64_t m_StoredTimestamp = 0;
	};
	std::unordered_map<const CHolder *, CLifetime> m_Active;
	std::array<CShadow, 16> m_aShadows;
	// One bounded lifecycle journal across main/dummy: heap addresses can migrate
	// between connections after a free. The journal must not hide that reuse.
	inline static std::array<CEvent, 64> s_aEvents;
	inline static std::mutex s_EventsMutex;
	inline static size_t s_NextEvent = 0, s_EventCount = 0;
	std::array<int, 16> m_aOnceSequence;
	size_t m_NextShadow = 0;
	uint64_t m_Check = 0;
	int m_Connection = -1, m_NetworkSequence = -1, m_State = -1;
	int m_CurrentTick = -1, m_BaseTick = -1;
	const char *m_pCurrentBoundary = "not_set";
	bool m_Sixup = false, m_WatchArmed = false;
	std::optional<CDebugMutation> m_LastMutation;
	bool m_PersistenceEnabled = false, m_FailurePersisted = false, m_PersistenceErrorReported = false;
	bool m_LogPathReady = false, m_LogPathPrepared = false;
	char m_aLogPath[IO_MAX_PATH_LENGTH] = {};
	std::mutex m_PersistMutex;

	CForensics() { m_aOnceSequence.fill(-1); }
	bool SetLogPath(const char *pPath)
	{
		if(m_LogPathReady || !pPath || !pPath[0])
			return false;
		str_copy(m_aLogPath, pPath, sizeof(m_aLogPath));
		m_LogPathReady = true;
		m_PersistenceEnabled = true;
		return true;
	}
	bool EnsureLogPath()
	{
		if(!m_LogPathReady)
		{
			char aUserPath[IO_MAX_PATH_LENGTH] = {};
			char aTimestamp[64];
			str_timestamp(aTimestamp, sizeof(aTimestamp));
			if(fs_storage_path("DDNet", aUserPath, sizeof(aUserPath)) != 0)
				return false;
			str_format(m_aLogPath, sizeof(m_aLogPath), "%s/dumps/AMF_snapshot_forensics_%s_%d_conn%d.log", aUserPath, aTimestamp, process_id(), m_Connection);
			m_LogPathReady = true;
		}
		if(!m_LogPathPrepared)
		{
			if(fs_makedir_rec_for(m_aLogPath) != 0)
				return false;
			m_LogPathPrepared = true;
		}
		return true;
	}
	bool PrepareForPersistence()
	{
		if(EnsureLogPath())
			return true;
		if(!m_PersistenceErrorReported)
		{
			m_PersistenceErrorReported = true;
			dbg_msg("snapshot_shadow", "durable forensic path unavailable path=%s connection=%d", m_aLogPath, m_Connection);
		}
		return false;
	}
	template<typename... TArgs>
	static void AppendLine(std::string &Out, const char *pFormat, TArgs... Args)
	{
		char aLine[1024];
		str_format(aLine, sizeof(aLine), pFormat, Args...);
		Out.append(aLine);
		Out.push_back('\n');
	}
	static void AppendHex(std::string &Out, const unsigned char *pBytes, size_t Size)
	{
		static constexpr char s_aHex[] = "0123456789abcdef";
		for(size_t i = 0; i < Size; ++i)
		{
			Out.push_back(s_aHex[pBytes[i] >> 4]);
			Out.push_back(s_aHex[pBytes[i] & 0xf]);
			if(i + 1 < Size)
				Out.push_back(' ');
		}
	}
	void AppendState(std::string &Out, const char *pReason) const
	{
		char aTimestamp[64];
		str_timestamp(aTimestamp, sizeof(aTimestamp));
		AppendLine(Out, "AMF snapshot forensics schema=1 persistence=io_sync_then_close timestamp=%s pid=%d connection=%d state=%d sixup=%d net_seq=%d current_tick=%d base_tick=%d boundary=%s reason=%s", aTimestamp, process_id(), m_Connection, m_State, m_Sixup, m_NetworkSequence, m_CurrentTick, m_BaseTick, m_pCurrentBoundary, pReason);
		AppendLine(Out, "log_path=%s tracked_limit=%zu lifecycle_limit=%zu holder_allocation_bytes=%zu", m_aLogPath, m_aShadows.size(), s_aEvents.size(), sizeof(CHolder));
		for(size_t i = 0; i < m_aShadows.size(); ++i)
		{
			const CShadow &S = m_aShadows[i];
			if(S.m_Bytes.empty())
				continue;
			const auto &Life = S.m_Lifetime;
			AppendLine(Out, "tracked slot=%zu alive=%d reported=%d stored_valid=%d stored_timestamp=%lld tick=%d connection=%d net_seq=%d items=%d data_size=%d holder=%p holder_gen=%llu holder_bytes=%zu raw=%p raw_gen=%llu raw_alloc_bytes=%zu alt=%p alt_gen=%llu alt_alloc_bytes=%zu add_seq=%llu stored_bytes=%zu stored_fingerprint=%016llx last_good=%s last_good_check=%llu last_good_tick=%d last_good_seq=%d first_bad=%s first_bad_check=%llu first_bad_tick=%d first_bad_seq=%d",
				i, S.m_Alive, S.m_Reported, S.m_StoredValid, (long long)S.m_StoredTimestamp, Life.m_Tick, Life.m_Connection, Life.m_NetworkSequence, S.m_NumItems, S.m_DataSize,
				reinterpret_cast<const void *>(Life.m_Holder), (unsigned long long)Life.m_HolderGeneration, sizeof(CHolder),
				reinterpret_cast<const void *>(Life.m_Raw), (unsigned long long)Life.m_RawGeneration, Life.m_Size,
				reinterpret_cast<const void *>(Life.m_Alt), (unsigned long long)Life.m_AltGeneration, Life.m_AltSize,
				(unsigned long long)Life.m_AddSequence, S.m_Bytes.size(), (unsigned long long)ShadowFingerprint(S.m_Bytes.data(), S.m_Bytes.size()),
				S.m_pLastGood, (unsigned long long)S.m_LastGoodCheck, S.m_LastGoodTick, S.m_LastGoodSequence,
				S.m_pFirstBad ? S.m_pFirstBad : "none", (unsigned long long)S.m_FirstBadCheck, S.m_FirstBadTick, S.m_FirstBadSequence);
		}
		const std::lock_guard<std::mutex> Lock(s_EventsMutex);
		AppendLine(Out, "lifecycle_events=%zu", s_EventCount);
		for(size_t i = 0; i < s_EventCount; ++i)
		{
			const CEvent &Event = s_aEvents[(s_NextEvent + s_aEvents.size() - s_EventCount + i) % s_aEvents.size()];
			const CLifetime &Life = Event.m_Lifetime;
			AppendLine(Out, "lifecycle timestamp=%lld seq=%llu reason=%s connection=%d tick=%d net_seq=%d holder=%p holder_gen=%llu holder_bytes=%zu raw=%p raw_gen=%llu raw_alloc_bytes=%zu alt=%p alt_gen=%llu alt_alloc_bytes=%zu add_seq=%llu",
				(long long)Event.m_Timestamp, (unsigned long long)Event.m_Sequence, Event.m_pReason, Life.m_Connection, Life.m_Tick, Life.m_NetworkSequence,
				reinterpret_cast<const void *>(Life.m_Holder), (unsigned long long)Life.m_HolderGeneration, sizeof(CHolder),
				reinterpret_cast<const void *>(Life.m_Raw), (unsigned long long)Life.m_RawGeneration, Life.m_Size,
				reinterpret_cast<const void *>(Life.m_Alt), (unsigned long long)Life.m_AltGeneration, Life.m_AltSize,
				(unsigned long long)Life.m_AddSequence);
		}
	}
	void AppendLifecycleForTarget(std::string &Out, const CLifetime &Target) const
	{
		const std::lock_guard<std::mutex> Lock(s_EventsMutex);
		AppendLine(Out, "target_lifecycle_events=%zu target_holder=%p target_holder_gen=%llu target_raw=%p target_raw_gen=%llu target_alt=%p target_alt_gen=%llu",
			s_EventCount, reinterpret_cast<const void *>(Target.m_Holder), (unsigned long long)Target.m_HolderGeneration,
			reinterpret_cast<const void *>(Target.m_Raw), (unsigned long long)Target.m_RawGeneration,
			reinterpret_cast<const void *>(Target.m_Alt), (unsigned long long)Target.m_AltGeneration);
		for(size_t i = 0; i < s_EventCount; ++i)
		{
			const CEvent &Event = s_aEvents[(s_NextEvent + s_aEvents.size() - s_EventCount + i) % s_aEvents.size()];
			const CLifetime &Life = Event.m_Lifetime;
			const auto MatchesTarget = [&Target](uintptr_t Address) {
				return Address && (Address == Target.m_Holder || Address == Target.m_Raw || Address == Target.m_Alt);
			};
			const bool SameAddress = MatchesTarget(Life.m_Holder) || MatchesTarget(Life.m_Raw) || MatchesTarget(Life.m_Alt);
			const auto ReusesTarget = [&Target](uintptr_t Address, uint64_t Generation) {
				return Address && ((Address == Target.m_Holder && Generation != Target.m_HolderGeneration) ||
					(Address == Target.m_Raw && Generation != Target.m_RawGeneration) ||
					(Address == Target.m_Alt && Generation != Target.m_AltGeneration));
			};
			const bool Replacement = ReusesTarget(Life.m_Holder, Life.m_HolderGeneration) ||
				ReusesTarget(Life.m_Raw, Life.m_RawGeneration) || ReusesTarget(Life.m_Alt, Life.m_AltGeneration);
			if(SameAddress || i + 16 >= s_EventCount)
				AppendLine(Out, "target_lifecycle timestamp=%lld seq=%llu reason=%s connection=%d tick=%d net_seq=%d holder=%p holder_gen=%llu raw=%p raw_gen=%llu raw_alloc_bytes=%zu alt=%p alt_gen=%llu alt_alloc_bytes=%zu add_seq=%llu same_address=%d replacement=%d",
					(long long)Event.m_Timestamp, (unsigned long long)Event.m_Sequence, Event.m_pReason, Life.m_Connection, Life.m_Tick, Life.m_NetworkSequence,
					reinterpret_cast<const void *>(Life.m_Holder), (unsigned long long)Life.m_HolderGeneration,
					reinterpret_cast<const void *>(Life.m_Raw), (unsigned long long)Life.m_RawGeneration, Life.m_Size,
					reinterpret_cast<const void *>(Life.m_Alt), (unsigned long long)Life.m_AltGeneration, Life.m_AltSize,
					(unsigned long long)Life.m_AddSequence, SameAddress, Replacement);
		}
	}
	bool WriteDocument(const std::string &Document)
	{
		if(Document.size() > std::numeric_limits<unsigned>::max())
			return false;
		IOHANDLE File = io_open(m_aLogPath, IOFLAG_WRITE);
		if(!File)
			return false;
		const unsigned Size = (unsigned)Document.size();
		bool Success = io_write(File, Document.data(), Size) == Size;
		if(Success)
			Success = io_sync(File) == 0;
		const bool Closed = io_close(File) == 0;
		return Success && Closed;
	}
	bool PersistCheckpoint(const char *pReason)
	{
		if(!m_PersistenceEnabled || m_FailurePersisted)
			return true;
		const std::lock_guard<std::mutex> Lock(m_PersistMutex);
		if(!PrepareForPersistence())
			return false;
		std::string Document;
		AppendState(Document, pReason);
		const bool Success = WriteDocument(Document);
		if(!Success && !m_PersistenceErrorReported)
		{
			m_PersistenceErrorReported = true;
			dbg_msg("snapshot_shadow", "durable checkpoint write failed path=%s", m_aLogPath);
		}
		return Success;
	}
	void AppendDiff(std::string &Out, const CShadow &S, const unsigned char *pLive, int Tick, int Sequence, const char *pBoundary) const
	{
		size_t First = S.m_Bytes.size(), Differences = 0, Ranges = 0;
		for(size_t i = 0; i < S.m_Bytes.size(); ++i)
			if(S.m_Bytes[i] != pLive[i])
			{
				First = std::min(First, i);
				++Differences;
				if(i == 0 || S.m_Bytes[i - 1] == pLive[i - 1])
					++Ranges;
			}
		AppendLine(Out, "snapshot_shadow: MUTATION connection=%d sixup=%d stored_tick=%d current_tick=%d base_tick=%d net_seq=%d holder=%p holder_gen=%llu raw=%p raw_gen=%llu allocation_bytes=%zu alt=%p alt_gen=%llu alt_allocation_bytes=%zu add_seq=%llu first_byte=%zu differing_bytes=%zu ranges=%zu stored_fingerprint=%016llx current_fingerprint=%016llx",
			S.m_Lifetime.m_Connection, m_Sixup, S.m_Lifetime.m_Tick, Tick, m_BaseTick, Sequence,
			reinterpret_cast<const void *>(S.m_Lifetime.m_Holder), (unsigned long long)S.m_Lifetime.m_HolderGeneration,
			reinterpret_cast<const void *>(S.m_Lifetime.m_Raw), (unsigned long long)S.m_Lifetime.m_RawGeneration, S.m_Lifetime.m_Size,
			reinterpret_cast<const void *>(S.m_Lifetime.m_Alt), (unsigned long long)S.m_Lifetime.m_AltGeneration, S.m_Lifetime.m_AltSize,
			(unsigned long long)S.m_Lifetime.m_AddSequence, First, Differences, Ranges,
			(unsigned long long)ShadowFingerprint(S.m_Bytes.data(), S.m_Bytes.size()), (unsigned long long)ShadowFingerprint(pLive, S.m_Bytes.size()));
		AppendLine(Out, "snapshot_shadow: INTERVAL last_good=%s check=%llu tick=%d net_seq=%d first_bad=%s check=%llu tick=%d net_seq=%d alive=%d replacement=0",
			S.m_pLastGood, (unsigned long long)S.m_LastGoodCheck, S.m_LastGoodTick, S.m_LastGoodSequence,
			S.m_pFirstBad ? S.m_pFirstBad : pBoundary,
			(unsigned long long)(S.m_pFirstBad ? S.m_FirstBadCheck : m_Check),
			S.m_pFirstBad ? S.m_FirstBadTick : Tick, S.m_pFirstBad ? S.m_FirstBadSequence : Sequence, S.m_Alive);
		for(size_t i = First; i < S.m_Bytes.size();)
		{
			if(S.m_Bytes[i] == pLive[i])
			{
				++i;
				continue;
			}
			const size_t Start = i;
			while(i < S.m_Bytes.size() && S.m_Bytes[i] != pLive[i])
				++i;
			AppendLine(Out, "changed_range=[%zu,%zu) bytes=%zu", Start, i, i - Start);
			Out += "before=";
			AppendHex(Out, S.m_Bytes.data() + Start, i - Start);
			Out += "\nafter=";
			AppendHex(Out, pLive + Start, i - Start);
			Out.push_back('\n');
		}
		if(S.m_Bytes.size() >= 96)
		{
			AppendLine(Out, "offsets20_21 address=%p offset=88 alignment8=%zu", pLive + 88, (uintptr_t)(pLive + 88) % 8);
			Out += "before=";
			AppendHex(Out, S.m_Bytes.data() + 88, 8);
			Out += "\nafter=";
			AppendHex(Out, pLive + 88, 8);
			Out.push_back('\n');
		}
		AppendLifecycleForTarget(Out, S.m_Lifetime);
	}
	bool PersistMutation(const CShadow &S, const unsigned char *pLive)
	{
		if(!m_PersistenceEnabled || m_FailurePersisted)
			return true;
		const std::lock_guard<std::mutex> Lock(m_PersistMutex);
		if(!PrepareForPersistence())
			return false;
		std::string Document;
		AppendState(Document, "mutation_detected");
		AppendDiff(Document, S, pLive, S.m_FirstBadTick, S.m_FirstBadSequence, S.m_pFirstBad ? S.m_pFirstBad : "mutation_detected");
		const bool Success = WriteDocument(Document);
		if(Success)
			m_FailurePersisted = true;
		else if(!m_PersistenceErrorReported)
		{
			m_PersistenceErrorReported = true;
			dbg_msg("snapshot_shadow", "durable mutation write failed path=%s", m_aLogPath);
		}
		return Success;
	}
	bool PersistEntityLinkFailure(const char *pRelation, const void *pWorld, const void *pEntity, int EntityType, int EntityId, const void *pParent, bool ParentBelongsToExpectedWorld, const void *pChild, bool ChildBelongsToExpectedWorld, const void *pInvalidLink, const void *pExpectedWorld, size_t WriteMemberOffset)
	{
		if(!m_PersistenceEnabled)
			return false;
		const std::lock_guard<std::mutex> Lock(m_PersistMutex);
		if(!PrepareForPersistence())
			return false;
		std::string Document;
		AppendState(Document, "invalid_entity_link_before_unlink");
		const uintptr_t WriteAddress = reinterpret_cast<uintptr_t>(pInvalidLink) + WriteMemberOffset;
		AppendLine(Document, "ENTITY_LINK_FAILURE timestamp_epoch=%lld connection=%d state=%d sixup=%d current_tick=%d base_tick=%d sequence=%d prediction_boundary=%s relation=%s world=%p entity=%p entity_type=%d entity_id=%d parent=%p parent_belongs_to_expected_world=%d child=%p child_belongs_to_expected_world=%d invalid_link=%p expected_world=%p write_member_offset=%zu calculated_write_address=%p",
			(long long)time_timestamp(), m_Connection, m_State, m_Sixup, m_CurrentTick, m_BaseTick, m_NetworkSequence, m_pCurrentBoundary,
			pRelation, pWorld, pEntity, EntityType, EntityId, pParent, ParentBelongsToExpectedWorld, pChild, ChildBelongsToExpectedWorld,
			pInvalidLink, pExpectedWorld, WriteMemberOffset, reinterpret_cast<const void *>(WriteAddress));
		for(size_t i = 0; i < m_aShadows.size(); ++i)
		{
			const CShadow &S = m_aShadows[i];
			if(S.m_Bytes.empty())
				continue;
			const uintptr_t Raw = S.m_Lifetime.m_Raw;
			const bool WriteHitsObservedOffset = Raw && WriteAddress == Raw + 88;
			AppendLine(Document, "entity_link_tracked_snapshot slot=%zu alive=%d tick=%d connection=%d holder=%p holder_gen=%llu raw=%p raw_gen=%llu raw_alloc_bytes=%zu alt=%p alt_gen=%llu alt_alloc_bytes=%zu add_seq=%llu suspect_equals_raw=%d calculated_write_equals_raw_plus_88=%d last_good=%s last_good_check=%llu",
				i, S.m_Alive, S.m_Lifetime.m_Tick, S.m_Lifetime.m_Connection,
				reinterpret_cast<const void *>(S.m_Lifetime.m_Holder), (unsigned long long)S.m_Lifetime.m_HolderGeneration,
				reinterpret_cast<const void *>(Raw), (unsigned long long)S.m_Lifetime.m_RawGeneration, S.m_Lifetime.m_Size,
				reinterpret_cast<const void *>(S.m_Lifetime.m_Alt), (unsigned long long)S.m_Lifetime.m_AltGeneration, S.m_Lifetime.m_AltSize,
				(unsigned long long)S.m_Lifetime.m_AddSequence, reinterpret_cast<uintptr_t>(pInvalidLink) == Raw,
				WriteHitsObservedOffset, S.m_pLastGood, (unsigned long long)S.m_LastGoodCheck);
		}
		const bool Success = WriteDocument(Document);
		if(Success)
			m_FailurePersisted = true;
		else if(!m_PersistenceErrorReported)
		{
			m_PersistenceErrorReported = true;
			dbg_msg("snapshot_shadow", "durable entity-link failure write failed path=%s", m_aLogPath);
		}
		return Success;
	}
	bool PersistInvalidBase(int Tick, int DeltaTick, int Sequence, bool Sixup, bool Dummy, const CSnapshot *pBase, int BaseSize)
	{
		if(!m_PersistenceEnabled)
			return false;
		const std::lock_guard<std::mutex> Lock(m_PersistMutex);
		if(!PrepareForPersistence())
			return false;
		std::string Document;
		AppendState(Document, "invalid_base_before_unpack");
		AppendLine(Document, "INVALID_BASE timestamp_epoch=%lld connection=%d state=%d current_tick=%d base_tick=%d sequence=%d sixup=%d dummy=%d base=%p reported_allocation_bytes=%d",
			(long long)time_timestamp(), m_Connection, m_State, Tick, DeltaTick, Sequence, Sixup, Dummy, static_cast<const void *>(pBase), BaseSize);
		const uintptr_t BaseAddress = reinterpret_cast<uintptr_t>(pBase);
		const auto Active = std::find_if(m_Active.begin(), m_Active.end(), [BaseAddress](const auto &Entry) { return Entry.second.m_Raw == BaseAddress; });
		const bool ActiveOwner = Active != m_Active.end() && Active->second.m_Size == (size_t)BaseSize;
		AppendLine(Document, "base_active_owner=%d base_allocation_size_matches=%d", Active != m_Active.end(), ActiveOwner);
		if(ActiveOwner && pBase && BaseSize >= (int)sizeof(CSnapshot) && BaseSize <= CSnapshot::MAX_SIZE)
		{
			AppendLine(Document, "base_metadata_num_items=%d base_metadata_data_size=%d", pBase->NumItems(), pBase->DataSize());
			const auto MatchingShadow = std::find_if(m_aShadows.begin(), m_aShadows.end(), [BaseAddress, DeltaTick, BaseSize](const CShadow &S) {
				return S.m_Lifetime.m_Raw == BaseAddress && S.m_Lifetime.m_Tick == DeltaTick && S.m_Bytes.size() == (size_t)BaseSize;
			});
			AppendLine(Document, "base_shadow_match=%d", MatchingShadow != m_aShadows.end());
			if(MatchingShadow != m_aShadows.end())
			{
				const CShadow &S = *MatchingShadow;
				const auto *pLive = reinterpret_cast<const unsigned char *>(pBase);
				const bool Changed = !std::equal(S.m_Bytes.begin(), S.m_Bytes.end(), pLive);
				AppendLine(Document, "base_shadow_comparison=%s stored_fingerprint=%016llx current_fingerprint=%016llx",
					Changed ? "MUTATION" : "IDENTICAL",
					(unsigned long long)ShadowFingerprint(S.m_Bytes.data(), S.m_Bytes.size()),
					(unsigned long long)ShadowFingerprint(pLive, S.m_Bytes.size()));
				if(Changed)
					AppendDiff(Document, S, pLive, Tick, Sequence, S.m_pFirstBad ? S.m_pFirstBad : "invalid_base_before_unpack");
				AppendLifecycleForTarget(Document, S.m_Lifetime);
			}
			else
			{
				const CLifetime &Life = Active->second;
				AppendLine(Document, "base_owner holder=%p holder_gen=%llu holder_bytes=%zu raw=%p raw_gen=%llu raw_alloc_bytes=%zu alt=%p alt_gen=%llu alt_alloc_bytes=%zu add_seq=%llu current_fingerprint=%016llx",
					reinterpret_cast<const void *>(Life.m_Holder), (unsigned long long)Life.m_HolderGeneration, sizeof(CHolder),
					reinterpret_cast<const void *>(Life.m_Raw), (unsigned long long)Life.m_RawGeneration, Life.m_Size,
					reinterpret_cast<const void *>(Life.m_Alt), (unsigned long long)Life.m_AltGeneration, Life.m_AltSize,
					(unsigned long long)Life.m_AddSequence, (unsigned long long)ShadowFingerprint(reinterpret_cast<const unsigned char *>(pBase), (size_t)BaseSize));
				AppendLifecycleForTarget(Document, Life);
			}
		}
		else
			AppendLine(Document, "%s", "base_shadow_comparison=unavailable_no_active_owner_or_untrusted_size");
		const bool Success = WriteDocument(Document);
		if(Success)
			m_FailurePersisted = true;
		else if(!m_PersistenceErrorReported)
		{
			m_PersistenceErrorReported = true;
			dbg_msg("snapshot_shadow", "durable invalid-base write failed path=%s", m_aLogPath);
		}
		return Success;
	}
	void Event(const CLifetime &Life, const char *pReason, uint64_t Sequence = 0)
	{
		const std::lock_guard<std::mutex> Lock(s_EventsMutex);
		s_aEvents[s_NextEvent] = {Life, Sequence ? Sequence : ++s_SnapshotLifetimeSequence, pReason, time_timestamp()};
		s_NextEvent = (s_NextEvent + 1) % s_aEvents.size();
		s_EventCount = std::min(s_EventCount + 1, s_aEvents.size());
	}
	void DumpEvents(const CLifetime &Life) const
	{
		const std::lock_guard<std::mutex> Lock(s_EventsMutex);
		for(size_t i = 0; i < s_EventCount; ++i)
		{
			const auto &Event = s_aEvents[(s_NextEvent + s_aEvents.size() - s_EventCount + i) % s_aEvents.size()];
			const auto &E = Event.m_Lifetime;
			const bool SameAddress = E.m_Raw == Life.m_Raw || E.m_Alt == Life.m_Raw || E.m_Holder == Life.m_Raw;
			if(SameAddress || i + 16 >= s_EventCount)
				dbg_msg("snapshot_shadow", "lifetime event=%llu reason=%s conn=%d tick=%d net_seq=%d holder=%p holder_gen=%llu raw=%p raw_gen=%llu size=%zu alt=%p alt_gen=%llu alt_size=%zu add=%llu same_address=%d replacement=0",
					(unsigned long long)Event.m_Sequence, Event.m_pReason, E.m_Connection, E.m_Tick, E.m_NetworkSequence,
					(void *)E.m_Holder, (unsigned long long)E.m_HolderGeneration, (void *)E.m_Raw, (unsigned long long)E.m_RawGeneration, E.m_Size,
					(void *)E.m_Alt, (unsigned long long)E.m_AltGeneration, E.m_AltSize, (unsigned long long)E.m_AddSequence, SameAddress);
		}
	}
};

void CSnapshotStorage::DebugSetContext(int Connection, int Sequence, int State, bool Sixup)
{
	if(!m_pForensics)
		m_pForensics = std::make_unique<CForensics>();
	m_pForensics->m_Connection = Connection;
	m_pForensics->m_NetworkSequence = Sequence;
	m_pForensics->m_State = State;
	m_pForensics->m_Sixup = Sixup;
}

void CSnapshotStorage::DebugSetPacketContext(int CurrentTick, int BaseTick)
{
	if(!m_pForensics)
		m_pForensics = std::make_unique<CForensics>();
	m_pForensics->m_CurrentTick = CurrentTick;
	m_pForensics->m_BaseTick = BaseTick;
}

void CSnapshotStorage::DebugEnableForensicsPersistence()
{
	if(!m_pForensics)
		m_pForensics = std::make_unique<CForensics>();
	m_pForensics->m_PersistenceEnabled = true;
}

bool CSnapshotStorage::DebugSetForensicsLogPath(const char *pPath)
{
	if(!m_pForensics)
		m_pForensics = std::make_unique<CForensics>();
	return m_pForensics->SetLogPath(pPath);
}

void CSnapshotStorage::DebugSetEntityLinkStorage(CSnapshotStorage *pStorage)
{
	s_pEntityLinkForensicsStorage = pStorage;
}

bool CSnapshotStorage::DebugPersistEntityLinkFailure(const char *pRelation, const void *pWorld, const void *pEntity, int EntityType, int EntityId, const void *pParent, bool ParentBelongsToExpectedWorld, const void *pChild, bool ChildBelongsToExpectedWorld, const void *pInvalidLink, const void *pExpectedWorld, size_t WriteMemberOffset)
{
	return s_pEntityLinkForensicsStorage && s_pEntityLinkForensicsStorage->m_pForensics &&
		s_pEntityLinkForensicsStorage->m_pForensics->PersistEntityLinkFailure(pRelation, pWorld, pEntity, EntityType, EntityId, pParent, ParentBelongsToExpectedWorld, pChild, ChildBelongsToExpectedWorld, pInvalidLink, pExpectedWorld, WriteMemberOffset);
}

bool CSnapshotStorage::DebugPersistInvalidBase(int Tick, int DeltaTick, int Sequence, bool Sixup, bool Dummy, const CSnapshot *pBase, int BaseSize)
{
	return m_pForensics && m_pForensics->PersistInvalidBase(Tick, DeltaTick, Sequence, Sixup, Dummy, pBase, BaseSize);
}

void CSnapshotStorage::DebugAdded(CHolder *pHolder)
{
	if(!m_pForensics)
		return;
	auto &F = *m_pForensics;
	CForensics::CLifetime Life;
	Life.m_Holder = (uintptr_t)pHolder;
	Life.m_Raw = (uintptr_t)pHolder->m_pSnap;
	Life.m_Alt = (uintptr_t)pHolder->m_pAltSnap;
	Life.m_Size = pHolder->m_SnapSize;
	Life.m_AltSize = pHolder->m_AltSnapSize;
	Life.m_HolderGeneration = ++s_SnapshotAllocationGeneration;
	Life.m_RawGeneration = ++s_SnapshotAllocationGeneration;
	Life.m_AltGeneration = Life.m_Alt ? ++s_SnapshotAllocationGeneration : 0;
	Life.m_Tick = pHolder->m_Tick;
	Life.m_Connection = F.m_Connection;
	Life.m_NetworkSequence = F.m_NetworkSequence;
	Life.m_AddSequence = ++s_SnapshotLifetimeSequence;
	F.Event(Life, "add", Life.m_AddSequence);
	F.m_Active.emplace(pHolder, Life);
	auto &S = F.m_aShadows[F.m_NextShadow];
	if(S.m_Alive)
		F.Event(S.m_Lifetime, "shadow_evicted_NOT_freed");
	S = {};
	S.m_Lifetime = Life;
	S.m_Alive = true;
	S.m_StoredValid = pHolder->m_pSnap->IsValid(pHolder->m_SnapSize);
	S.m_NumItems = pHolder->m_pSnap->NumItems();
	S.m_DataSize = pHolder->m_pSnap->DataSize();
	S.m_StoredTimestamp = time_timestamp();
	S.m_Bytes.assign((const unsigned char *)pHolder->m_pSnap, (const unsigned char *)pHolder->m_pSnap + pHolder->m_SnapSize);
	S.m_LastGoodCheck = ++F.m_Check;
	S.m_LastGoodTick = Life.m_Tick;
	S.m_LastGoodSequence = Life.m_NetworkSequence;
	F.m_NextShadow = (F.m_NextShadow + 1) % F.m_aShadows.size();

	const char *pMinTick = std::getenv("AMF_SNAPSHOT_WATCH_TICK");
	const char *pMinItems = std::getenv("AMF_SNAPSHOT_WATCH_MIN_ITEMS");
	if(!F.m_WatchArmed && (pMinTick || pMinItems) && pHolder->m_SnapSize >= 96 && pHolder->m_pSnap->IsValid(pHolder->m_SnapSize) &&
		pHolder->m_pSnap->NumItems() > 21 && (!pMinTick || Life.m_Tick >= std::atoi(pMinTick)) &&
		(!pMinItems || pHolder->m_pSnap->NumItems() >= std::atoi(pMinItems)))
	{
		F.m_WatchArmed = true;
		const void *pWatch = (const unsigned char *)pHolder->m_pSnap + 88;
		dbg_msg("snapshot_shadow", "WATCH conn=%d tick=%d holder=%p holder_gen=%llu raw=%p raw_gen=%llu bytes=%zu alt=%p offsets20=%p offsets21=%p alignment8=%zu add=%llu",
			Life.m_Connection, Life.m_Tick, pHolder, (unsigned long long)Life.m_HolderGeneration, pHolder->m_pSnap,
			(unsigned long long)Life.m_RawGeneration, Life.m_Size, pHolder->m_pAltSnap, pWatch, (const unsigned char *)pWatch + 4,
			(uintptr_t)pWatch % 8, (unsigned long long)Life.m_AddSequence);
		SnapshotForensicsWatchpoint(pHolder, pWatch, Life.m_RawGeneration);
	}
	F.PersistCheckpoint("snapshot_added");
}

void CSnapshotStorage::DebugFreeing(CHolder *pHolder, const char *pReason)
{
	if(!m_pForensics)
		return;
	auto &F = *m_pForensics;
	const auto It = F.m_Active.find(pHolder);
	if(It == F.m_Active.end())
		return;
	F.Event(It->second, pReason); // recorded BEFORE free; no freed allocation is read by checks
	for(auto &S : F.m_aShadows)
		if(S.m_Lifetime.m_RawGeneration == It->second.m_RawGeneration)
			S.m_Alive = false;
	F.m_Active.erase(It);
	F.PersistCheckpoint(pReason);
}

uint64_t CSnapshotStorage::DebugAllocationGeneration(const CHolder *pHolder) const
{
	if(!m_pForensics)
		return 0;
	const auto It = m_pForensics->m_Active.find(pHolder);
	return It == m_pForensics->m_Active.end() ? 0 : It->second.m_RawGeneration;
}

size_t CSnapshotStorage::DebugShadowCount() const
{
	if(!m_pForensics)
		return 0;
	return std::count_if(m_pForensics->m_aShadows.begin(), m_pForensics->m_aShadows.end(), [](const auto &S) { return S.m_Alive; });
}

std::optional<CSnapshotStorage::CDebugMutation> CSnapshotStorage::DebugLastMutation() const
{
	return m_pForensics ? m_pForensics->m_LastMutation : std::nullopt;
}

bool CSnapshotStorage::DebugCheck(const char *pBoundary, int Tick, int Sequence, int OnceSlot)
{
	if(!m_pForensics)
		return true;
	auto &F = *m_pForensics;
	F.m_CurrentTick = Tick;
	F.m_pCurrentBoundary = pBoundary;
	if(OnceSlot >= 0)
	{
		dbg_assert(OnceSlot < (int)F.m_aOnceSequence.size(), "Invalid shadow diagnostic checkpoint slot");
		if(F.m_aOnceSequence[OnceSlot] == Sequence)
			return true;
		F.m_aOnceSequence[OnceSlot] = Sequence;
	}
	const uint64_t Check = ++F.m_Check;
	bool Intact = true;
	for(auto &S : F.m_aShadows)
	{
		if(!S.m_Alive)
			continue;
		const auto &Life = S.m_Lifetime;
		const auto It = F.m_Active.find((const CHolder *)Life.m_Holder);
		if(It == F.m_Active.end() || It->second.m_RawGeneration != Life.m_RawGeneration)
		{
			if(!S.m_Reported)
				dbg_msg("snapshot_shadow", "ownership mismatch BEFORE read conn=%d gen=%llu boundary=%s", F.m_Connection, (unsigned long long)Life.m_RawGeneration, pBoundary);
			S.m_Reported = true;
			Intact = false;
			continue;
		}
		const auto *pHolder = (const CHolder *)Life.m_Holder;
		if((uintptr_t)pHolder->m_pSnap != Life.m_Raw || (uintptr_t)pHolder->m_pAltSnap != Life.m_Alt ||
			pHolder->m_SnapSize != (int)Life.m_Size || pHolder->m_AltSnapSize != (int)Life.m_AltSize || pHolder->m_Tick != Life.m_Tick)
		{
			if(!S.m_Reported)
			{
				dbg_msg("snapshot_shadow", "holder changed BEFORE raw read conn=%d boundary=%s holder=%p holder_gen=%llu raw_gen=%llu stored_raw=%p current_raw=%p stored_alt=%p current_alt=%p stored_size=%zu current_size=%d stored_tick=%d current_tick=%d",
					F.m_Connection, pBoundary, pHolder, (unsigned long long)Life.m_HolderGeneration, (unsigned long long)Life.m_RawGeneration,
					(void *)Life.m_Raw, pHolder->m_pSnap, (void *)Life.m_Alt, pHolder->m_pAltSnap, Life.m_Size, pHolder->m_SnapSize, Life.m_Tick, pHolder->m_Tick);
				F.DumpEvents(Life);
			}
			S.m_Reported = true;
			Intact = false;
			continue;
		}
		const auto *pLive = (const unsigned char *)Life.m_Raw;
		size_t First = S.m_Bytes.size(), Differences = 0, Ranges = 0;
		for(size_t i = 0; i < S.m_Bytes.size(); ++i)
			if(S.m_Bytes[i] != pLive[i])
			{
				First = std::min(First, i);
				++Differences;
				if(i == 0 || S.m_Bytes[i - 1] == pLive[i - 1])
					++Ranges;
			}
		if(Differences == 0)
		{
			S.m_pLastGood = pBoundary;
			S.m_LastGoodCheck = Check;
			S.m_LastGoodTick = Tick;
			S.m_LastGoodSequence = Sequence;
			continue;
		}
		Intact = false;
		if(S.m_Reported)
			return false;
		S.m_Reported = true;
		S.m_pFirstBad = pBoundary;
		S.m_FirstBadCheck = Check;
		S.m_FirstBadTick = Tick;
		S.m_FirstBadSequence = Sequence;
		F.m_LastMutation = CDebugMutation{First, Differences, Ranges, Life.m_RawGeneration, S.m_pLastGood, pBoundary};
		dbg_msg("snapshot_shadow", "MUTATION conn=%d state=%d sixup=%d stored_tick=%d tick=%d net_seq=%d holder=%p holder_gen=%llu raw=%p raw_gen=%llu size=%zu alt=%p alt_gen=%llu add=%llu first_byte=%zu differing_bytes=%zu ranges=%zu stored_hash=%016llx live_hash=%016llx",
			F.m_Connection, F.m_State, F.m_Sixup, Life.m_Tick, Tick, Sequence, (void *)Life.m_Holder, (unsigned long long)Life.m_HolderGeneration,
			(void *)Life.m_Raw, (unsigned long long)Life.m_RawGeneration, Life.m_Size, (void *)Life.m_Alt, (unsigned long long)Life.m_AltGeneration,
			(unsigned long long)Life.m_AddSequence, First, Differences, Ranges,
			(unsigned long long)ShadowFingerprint(S.m_Bytes.data(), S.m_Bytes.size()), (unsigned long long)ShadowFingerprint(pLive, S.m_Bytes.size()));
		dbg_msg("snapshot_shadow", "INTERVAL last_good=%s check=%llu tick=%d net_seq=%d first_bad=%s check=%llu tick=%d net_seq=%d alive=1 replacement=0",
			S.m_pLastGood, (unsigned long long)S.m_LastGoodCheck, S.m_LastGoodTick, S.m_LastGoodSequence, pBoundary, (unsigned long long)Check, Tick, Sequence);
		size_t Printed = 0;
		for(size_t i = First; i < S.m_Bytes.size();)
		{
			if(S.m_Bytes[i] == pLive[i]) { ++i; continue; }
			const size_t Start = i;
			while(i < S.m_Bytes.size() && S.m_Bytes[i] != pLive[i]) ++i;
			if(Printed++ < 16)
			{
				char aBefore[97] = {}, aAfter[97] = {};
				const size_t Bytes = std::min(i - Start, (size_t)32);
				for(size_t j = 0; j < Bytes; ++j)
				{
					str_format(aBefore + j * 3, 4, "%02x ", (unsigned)S.m_Bytes[Start + j]);
					str_format(aAfter + j * 3, 4, "%02x ", (unsigned)pLive[Start + j]);
				}
				dbg_msg("snapshot_shadow", "range=[%zu,%zu) before=%s after=%s shown=%zu", Start, i, aBefore, aAfter, Bytes);
			}
		}
		if(Life.m_Size >= 96)
		{
			char aBefore[25] = {}, aAfter[25] = {};
			for(size_t j = 0; j < 8; ++j)
			{
				str_format(aBefore + j * 3, 4, "%02x ", (unsigned)S.m_Bytes[88 + j]);
				str_format(aAfter + j * 3, 4, "%02x ", (unsigned)pLive[88 + j]);
			}
			dbg_msg("snapshot_shadow", "offsets20_21 address=%p offset=88 alignment8=%zu before=%s after=%s holder_bytes=%zu raw_allocation_start=%p raw_allocation_offset=88",
				pLive + 88, (uintptr_t)(pLive + 88) % 8, aBefore, aAfter, sizeof(CHolder), pLive);
		}
		F.DumpEvents(Life);
		for(const auto &Recent : F.m_aShadows)
			if(!Recent.m_Bytes.empty())
				dbg_msg("snapshot_shadow", "recent tick=%d holder=%p raw=%p gen=%llu alive=%d add=%llu hash=%016llx reported_mutated=%d", Recent.m_Lifetime.m_Tick,
					(void *)Recent.m_Lifetime.m_Holder, (void *)Recent.m_Lifetime.m_Raw, (unsigned long long)Recent.m_Lifetime.m_RawGeneration, Recent.m_Alive,
					(unsigned long long)Recent.m_Lifetime.m_AddSequence, (unsigned long long)ShadowFingerprint(Recent.m_Bytes.data(), Recent.m_Bytes.size()), Recent.m_Reported);
		F.PersistMutation(S, pLive);
		return false;
	}
	return Intact;
}
#endif

CSnapshotStorage::CSnapshotStorage() { Init(); }
CSnapshotStorage::~CSnapshotStorage()
{
#if defined(CONF_SNAPSHOT_FORENSICS)
	if(s_pEntityLinkForensicsStorage == this)
		s_pEntityLinkForensicsStorage = nullptr;
#endif
	PurgeAll();
}

void CSnapshotStorage::Init()
{
	m_pFirst = nullptr;
	m_pLast = nullptr;
}

void CSnapshotStorage::PurgeAll()
{
#if defined(CONF_SNAPSHOT_FORENSICS)
	DebugCheck("before_purge_all", -1, m_pForensics ? m_pForensics->m_NetworkSequence : -1);
#endif
	while(m_pFirst)
	{
		CHolder *pNext = m_pFirst->m_pNext;
#if defined(CONF_SNAPSHOT_FORENSICS)
		DebugFreeing(m_pFirst, "purge_all_free");
#endif
		free(m_pFirst->m_pSnap);
		free(m_pFirst->m_pAltSnap);
		free(m_pFirst);
		m_pFirst = pNext;
	}
	m_pLast = nullptr;
#if defined(CONF_SNAPSHOT_FORENSICS)
	if(m_pForensics)
		m_pForensics->m_aOnceSequence.fill(-1);
#endif
}

void CSnapshotStorage::PurgeUntil(int Tick)
{
#if defined(CONF_SNAPSHOT_FORENSICS)
	DebugCheck("before_purge_until", Tick, m_pForensics ? m_pForensics->m_NetworkSequence : -1);
#endif
	CHolder *pHolder = m_pFirst;

	while(pHolder)
	{
		CHolder *pNext = pHolder->m_pNext;
		if(pHolder->m_Tick >= Tick)
			return; // no more to remove
#if defined(CONF_SNAPSHOT_FORENSICS)
		DebugFreeing(pHolder, "purge_until_free");
#endif
		free(pHolder->m_pSnap);
		free(pHolder->m_pAltSnap);
		free(pHolder);

		// did we come to the end of the list?
		if(!pNext)
			break;

		m_pFirst = pNext;
		pNext->m_pPrev = nullptr;
		pHolder = pNext;
	}

	// no more snapshots in storage
	m_pFirst = nullptr;
	m_pLast = nullptr;
}

void CSnapshotStorage::Add(int Tick, int64_t Tagtime, size_t DataSize, const void *pData, size_t AltDataSize, const void *pAltData)
{
	dbg_assert(DataSize <= (size_t)CSnapshot::MAX_SIZE, "Snapshot data size invalid");
	dbg_assert(AltDataSize <= (size_t)CSnapshot::MAX_SIZE, "Alt snapshot data size invalid");

	CHolder *pHolder = static_cast<CHolder *>(malloc(sizeof(CHolder)));
	pHolder->m_Tick = Tick;
	pHolder->m_Tagtime = Tagtime;

	pHolder->m_pSnap = static_cast<CSnapshot *>(malloc(DataSize));
	mem_copy(pHolder->m_pSnap, pData, DataSize);
	pHolder->m_SnapSize = DataSize;

	if(AltDataSize) // create alternative if wanted
	{
		pHolder->m_pAltSnap = static_cast<CSnapshot *>(malloc(AltDataSize));
		mem_copy(pHolder->m_pAltSnap, pAltData, AltDataSize);
		pHolder->m_AltSnapSize = AltDataSize;
	}
	else
	{
		pHolder->m_pAltSnap = nullptr;
		pHolder->m_AltSnapSize = 0;
	}

	// link
	pHolder->m_pNext = nullptr;
	pHolder->m_pPrev = m_pLast;
	if(m_pLast)
	{
		dbg_assert(m_pLast->m_Tick < Tick, "snapshots inserted into CSnapshotStorage with non-increasing tick %d >= %d", m_pLast->m_Tick, Tick);
		m_pLast->m_pNext = pHolder;
	}
	else
	{
		m_pFirst = pHolder;
	}
	m_pLast = pHolder;
#if defined(CONF_SNAPSHOT_FORENSICS)
	DebugAdded(pHolder);
	DebugCheck("after_add", Tick, m_pForensics ? m_pForensics->m_NetworkSequence : -1);
#endif
}

int CSnapshotStorage::Get(int Tick, int64_t *pTagtime, const CSnapshot **ppData, const CSnapshot **ppAltData) const
{
	// the list is sorted by tick and the queried tick is usually one of the
	// most recently added ones, so search backwards starting at the newest
	CHolder *pHolder = m_pLast;

	while(pHolder)
	{
		if(pHolder->m_Tick == Tick)
		{
			if(pTagtime)
				*pTagtime = pHolder->m_Tagtime;
			if(ppData)
				*ppData = pHolder->m_pSnap;
			if(ppAltData)
				*ppAltData = pHolder->m_pAltSnap;
			return pHolder->m_SnapSize;
		}
		if(pHolder->m_Tick < Tick)
			return -1; // all remaining snapshots are even older

		pHolder = pHolder->m_pPrev;
	}

	return -1;
}

// CSnapshotBuilder
void CSnapshotBuilder::Init(bool Sixup)
{
	dbg_assert(!m_Building, "Snapshot builder is already building snapshot. Call `Finish` for each call to `Init`.");

	m_DataSize = 0;
	m_NumItems = 0;
	m_Building = true;
	m_HasDroppedItem = false;
	m_Sixup = Sixup;
	m_DebugContext = {};
	m_DebugContext.m_pStage = "snapshot-builder";

	for(int i = 0; i < m_NumExtendedItemTypes; i++)
	{
		AddExtendedItemType(i);
	}
}

CSnapshotItem *CSnapshotBuilder::GetItem(int Index)
{
	dbg_assert(0 <= Index && Index < m_NumItems, "invalid item index");
	return (CSnapshotItem *)&(m_aData[m_aOffsets[Index]]);
}

int CSnapshotBuilder::GetItemSize(int Index) const
{
	dbg_assert(0 <= Index && Index < m_NumItems, "invalid item index");
	int Start = m_aOffsets[Index];
	int End = Index + 1 < m_NumItems ? m_aOffsets[Index + 1] : m_DataSize;
	return (End - Start) - sizeof(CSnapshotItem);
}

int *CSnapshotBuilder::GetItemData(int Index)
{
	return GetItem(Index)->Data();
}

std::optional<int> CSnapshotBuilder::FindItemIndexByKey(int Key)
{
	for(int i = 0; i < m_NumItems; i++)
	{
		CSnapshotItem *pItem = GetItem(i);
		if(pItem->Key() == Key)
		{
			return i;
		}
	}
	return std::nullopt;
}

int CSnapshotBuilder::FinishIfNoDroppedItems(CSnapshotBuffer *pSnapData)
{
	dbg_assert(m_Building, "Snapshot builder is not building snapshot. Call `FinishIfNoDroppedItems` after `Init`.");
	if(m_HasDroppedItem)
	{
		m_Building = false;
		return -1;
	}
	return Finish(pSnapData);
}

int CSnapshotBuilder::Finish(CSnapshotBuffer *pBuffer)
{
	dbg_assert(m_Building, "Snapshot builder is not building snapshot. Call `Finish` after `Init`.");
	m_Building = false;

	// flatten and make the snapshot
	dbg_assert(m_NumItems <= CSnapshot::MAX_ITEMS, "Too many snap items");
	CSnapshot *pSnap = pBuffer->AsSnapshot();
	pSnap->m_DataSize = m_DataSize;
	pSnap->m_NumItems = m_NumItems;
	const size_t TotalSize = pSnap->TotalSize();
	dbg_assert(TotalSize <= (size_t)CSnapshot::MAX_SIZE, "Snapshot too large");
	mem_copy(pSnap->Offsets(), m_aOffsets, pSnap->OffsetSize());
	mem_copy(pSnap->DataStart(), m_aData, m_DataSize);
	return TotalSize;
}

int CSnapshotBuilder::GetTypeFromIndex(int Index) const
{
	return CSnapshot::MAX_TYPE - Index;
}

bool CSnapshotBuilder::AddExtendedItemType(int Index)
{
	dbg_assert(m_Building, "Snapshot builder is not building snapshot. Call `AddExtendedItemType` between `Init` and `Finish`.");
	dbg_assert(0 <= Index && Index < m_NumExtendedItemTypes, "Index out of range: %d", Index);

	int *pUuidItem = static_cast<int *>(NewItemRaw(0, GetTypeFromIndex(Index), sizeof(CUuid))); // NETOBJTYPE_EX
	if(pUuidItem == nullptr)
	{
		return false;
	}

	const int TypeId = m_aExtendedItemTypes[Index];
	const CUuid Uuid = g_UuidManager.GetUuid(TypeId);
	for(size_t i = 0; i < sizeof(CUuid) / sizeof(int32_t); i++)
	{
		pUuidItem[i] = bytes_be_to_uint(&Uuid.m_aData[i * sizeof(int32_t)]);
	}
	return true;
}

int CSnapshotBuilder::GetExtendedItemTypeIndex(int TypeId)
{
	for(int i = 0; i < m_NumExtendedItemTypes; i++)
	{
		if(m_aExtendedItemTypes[i] == TypeId)
		{
			return i;
		}
	}
	dbg_assert(m_NumExtendedItemTypes < MAX_EXTENDED_ITEM_TYPES, "too many extended item types");
	int Index = m_NumExtendedItemTypes;
	m_NumExtendedItemTypes++;
	m_aExtendedItemTypes[Index] = TypeId;
	if(AddExtendedItemType(Index))
	{
		return Index;
	}
	m_NumExtendedItemTypes--;
	return -1;
}

bool CSnapshotBuilder::NewItem(int Type, int Id, const void *pData, int Size)
{
	dbg_assert(m_Building, "Snapshot builder is not building snapshot. Call `NewItem` between `Init` and `Finish`.");
	if(m_HasDroppedItem)
	{
		return false;
	}
	void *pUninitData = NewItemRaw(Type, Id, Size);
	if(!pUninitData)
	{
		m_HasDroppedItem = true;
		return false;
	}
	mem_copy(pUninitData, pData, Size);
	return true;
}

void *CSnapshotBuilder::NewItemRaw(int Type, int Id, int Size)
{
	dbg_assert(m_Building, "Snapshot builder is not building snapshot. Call `NewItemRaw` between `Init` and `Finish`.");
	const bool Extended = Type >= OFFSET_UUID;
	dbg_assert((Type >= 0 && Type <= CSnapshot::MAX_TYPE) || Extended || (m_Sixup && Type >= -CSnapshot::MAX_TYPE && Type < 0), "Invalid snap item Type: %d", Type);
	dbg_assert(Id >= 0 && Id <= CSnapshot::MAX_ID, "Invalid snap item Id: %d", Id);
	if(Size < 0 || (size_t)Size > CSnapshot::MAX_SIZE - sizeof(CSnapshot) - sizeof(CSnapshotItem) - sizeof(int) || Size % sizeof(int32_t) != 0)
	{
		// Deliberately log only on the fatal path; do not alter the validation or
		// continue with the malformed item. This is temporary crash-forensics.
		dbg_msg("snapshot-corruption",
			"invalid item rejected: stage=%s tick=%d delta_tick=%d conn=%d seq=%d compressed=%d delta_size=%d sixup=%d dummy=%d type=%d id=%d size=%d builder_items=%d builder_data=%d",
			m_DebugContext.m_pStage ? m_DebugContext.m_pStage : "unspecified",
			m_DebugContext.m_GameTick,
			m_DebugContext.m_DeltaTick,
			m_DebugContext.m_Connection,
			m_DebugContext.m_Sequence,
			m_DebugContext.m_CompressedSize,
			m_DebugContext.m_DeltaSize,
			m_DebugContext.m_Sixup,
			m_DebugContext.m_Dummy,
			Type,
			Id,
			Size,
			m_NumItems,
			m_DataSize);
	}
	dbg_assert(Size >= 0 && (size_t)Size <= CSnapshot::MAX_SIZE - sizeof(CSnapshot) - sizeof(CSnapshotItem) - sizeof(int) && Size % sizeof(int32_t) == 0, "Invalid snap item Size: %d", Size);

	// resolve extended types first for capacity checks below
	if(Extended)
	{
		const int ExtendedItemTypeIndex = GetExtendedItemTypeIndex(Type);
		if(ExtendedItemTypeIndex == -1)
		{
			return nullptr;
		}
		Type = GetTypeFromIndex(ExtendedItemTypeIndex);
	}

	if(m_NumItems >= CSnapshot::MAX_ITEMS)
	{
		return nullptr;
	}

	const size_t OffsetSize = (m_NumItems + 1) * sizeof(int);
	const size_t ItemSize = sizeof(CSnapshotItem) + Size;
	if(sizeof(CSnapshot) + OffsetSize + m_DataSize + ItemSize > CSnapshot::MAX_SIZE)
	{
		return nullptr;
	}

	CSnapshotItem *pObj = (CSnapshotItem *)(m_aData + m_DataSize);

	if(m_Sixup && !Extended)
	{
		if(Type >= 0)
			Type = Obj_SixToSeven(Type);
		else
			Type *= -1;

		if(Type < 0)
			return pObj;
	}
	else if(Type < 0)
		return nullptr;

	pObj->m_TypeAndId = (Type << 16) | Id;
	m_aOffsets[m_NumItems] = m_DataSize;
	m_DataSize += ItemSize;
	m_NumItems++;

	mem_zero(pObj->Data(), Size);
	return pObj->Data();
}
