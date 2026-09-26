// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCOscParser.h"

namespace VMCOscParser
{
	namespace
	{
		constexpr int32 MaxBundleDepth = 8;
		constexpr int32 MaxArgs = 64; // bounds the argument array (VMC sends at most 14)

		float BitsToFloat(uint32 Bits)
		{
			float F;
			FMemory::Memcpy(&F, &Bits, sizeof(F));
			return F;
		}

		double BitsToDouble(uint64 Bits)
		{
			double D;
			FMemory::Memcpy(&D, &Bits, sizeof(D));
			return D;
		}

		int32 Pad4(int32 N)
		{
			return (N + 3) & ~3;
		}

		/** A null-terminated string padded to 4 bytes, starting at Pos. */
		bool ReadString(TConstArrayView<uint8> Data, int32& Pos, const uint8*& OutStart, int32& OutLen)
		{
			if (Pos < 0 || Pos >= Data.Num())
			{
				return false;
			}
			const uint8* Start = Data.GetData() + Pos;
			const int32 Max = Data.Num() - Pos;
			int32 Len = 0;
			while (Len < Max && Start[Len] != 0)
			{
				++Len;
			}
			if (Len == Max)
			{
				return false; // no terminator
			}
			const int32 Next = Pos + Pad4(Len + 1);
			if (Next > Data.Num())
			{
				return false;
			}
			OutStart = Start;
			OutLen = Len;
			Pos = Next;
			return true;
		}

		bool ReadU32(TConstArrayView<uint8> Data, int32& Pos, uint32& Out)
		{
			if (Pos < 0 || Pos + 4 > Data.Num())
			{
				return false;
			}
			const uint8* P = Data.GetData() + Pos;
			Out = (uint32(P[0]) << 24) | (uint32(P[1]) << 16) | (uint32(P[2]) << 8) | uint32(P[3]);
			Pos += 4;
			return true;
		}

		bool ReadU64(TConstArrayView<uint8> Data, int32& Pos, uint64& Out)
		{
			uint32 Hi = 0, Lo = 0;
			if (!ReadU32(Data, Pos, Hi) || !ReadU32(Data, Pos, Lo))
			{
				return false;
			}
			Out = (uint64(Hi) << 32) | Lo;
			return true;
		}

		bool ParseMessage(TConstArrayView<uint8> Data, FOnMessage OnMessage, VMCProtocol::FArgs& Args)
		{
			using VMCProtocol::FArg;
			int32 Pos = 0;
			const uint8* Address = nullptr;
			int32 AddressLen = 0;
			if (!ReadString(Data, Pos, Address, AddressLen) || AddressLen == 0 || Address[0] != '/')
			{
				return false;
			}

			Args.Reset();
			if (Pos < Data.Num())
			{
				const uint8* Tags = nullptr;
				int32 TagsLen = 0;
				if (!ReadString(Data, Pos, Tags, TagsLen) || TagsLen == 0 || Tags[0] != ',')
				{
					return false;
				}
				for (int32 t = 1; t < TagsLen; ++t)
				{
					if (Args.Num() >= MaxArgs)
					{
						return false;
					}
					uint32 U32 = 0;
					uint64 U64 = 0;
					switch (Tags[t])
					{
					case 'i':
						if (!ReadU32(Data, Pos, U32)) return false;
						Args.Add(FArg::MakeInt(int32(U32)));
						break;
					case 'f':
						if (!ReadU32(Data, Pos, U32)) return false;
						Args.Add(FArg::MakeFloat(BitsToFloat(U32)));
						break;
					case 's':
					case 'S':
					{
						const uint8* Str = nullptr;
						int32 StrLen = 0;
						if (!ReadString(Data, Pos, Str, StrLen)) return false;
						Args.Add(FArg::MakeUtf8(FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Str), StrLen)));
						break;
					}
					case 'b':
					{
						if (!ReadU32(Data, Pos, U32)) return false;
						const int32 Size = int32(U32);
						if (Size < 0 || Size > Data.Num() - Pos || Pos + Pad4(Size) > Data.Num()) return false;
						Pos += Pad4(Size);
						Args.Add(FArg());
						break;
					}
					case 'd':
						if (!ReadU64(Data, Pos, U64)) return false;
						Args.Add(FArg::MakeFloat(float(BitsToDouble(U64))));
						break;
					case 'h':
						if (!ReadU64(Data, Pos, U64)) return false;
						Args.Add(FArg::MakeInt(int32(FMath::Clamp<int64>(int64(U64), MIN_int32, MAX_int32))));
						break;
					case 't':
						if (!ReadU64(Data, Pos, U64)) return false;
						Args.Add(FArg());
						break;
					case 'c':
					case 'r':
					case 'm':
						if (!ReadU32(Data, Pos, U32)) return false;
						Args.Add(FArg());
						break;
					case 'T':
					case 'F':
					case 'N':
					case 'I':
						Args.Add(FArg()); // no data
						break;
					case '[':
					case ']':
						break; // array markers: the elements follow as ordinary arguments
					default:
						return false;
					}
				}
			}

			OnMessage(FAnsiStringView(reinterpret_cast<const ANSICHAR*>(Address), AddressLen), Args);
			return true;
		}

		bool ParseElement(TConstArrayView<uint8> Data, int32 Depth, FOnMessage OnMessage, VMCProtocol::FArgs& Args)
		{
			static const uint8 BundleTag[8] = { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0 };
			if (Data.Num() >= 8 && FMemory::Memcmp(Data.GetData(), BundleTag, 8) == 0)
			{
				if (Depth >= MaxBundleDepth || Data.Num() < 16)
				{
					return false;
				}
				int32 Pos = 16; // tag + 8-byte time tag
				bool bOk = true;
				while (Pos < Data.Num())
				{
					uint32 U32 = 0;
					if (!ReadU32(Data, Pos, U32))
					{
						return false;
					}
					const int32 Size = int32(U32);
					if (Size < 0 || Size > Data.Num() - Pos)
					{
						return false; // the rest can't be trusted
					}
					if (!ParseElement(Data.Slice(Pos, Size), Depth + 1, OnMessage, Args))
					{
						bOk = false; // this element is skipped; the next one starts at a known offset
					}
					Pos += Size;
				}
				return bOk;
			}
			return ParseMessage(Data, OnMessage, Args);
		}
	}

	bool ParsePacket(TConstArrayView<uint8> Packet, FOnMessage OnMessage)
	{
		VMCProtocol::FArgs Args;
		return ParseElement(Packet, 0, OnMessage, Args);
	}
}
