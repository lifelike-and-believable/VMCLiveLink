// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VMCProtocol.h"

/**
 * OSC 1.0 packet parsing (https://opensoundcontrol.stanford.edu/spec-1_0.html) for the VMC receive
 * thread (P3.1 step 2). The OSC plugin's packet parser isn't public, and this one reads in place:
 * addresses and strings are views into the packet, so parsing a message allocates nothing.
 *
 * Supported argument types: i f s S b h d t c r m T F N I and array brackets. VMC uses i, f and s;
 * the others are read past (d as a number, h as an int, the rest as "other"). Unknown type tags make
 * the message malformed.
 */
namespace VMCOscParser
{
	/** Called for each message: its address and arguments, valid only during the call. */
	using FOnMessage = TFunctionRef<void(FAnsiStringView Address, TConstArrayView<VMCProtocol::FArg> Args)>;

	/**
	 * Parses a packet (a message, or a bundle of messages and bundles) and calls OnMessage for every
	 * well-formed message in it, in order. Returns false if anything in it was malformed; the
	 * well-formed messages are still delivered.
	 */
	bool ParsePacket(TConstArrayView<uint8> Packet, FOnMessage OnMessage);
}
