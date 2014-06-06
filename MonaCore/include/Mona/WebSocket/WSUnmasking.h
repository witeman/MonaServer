/*
Copyright 2014 Mona
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

This file is a part of Mona.
*/

#pragma once

#include "Mona/Mona.h"
#include "Mona/Decoding.h"
#include "Mona/WebSocket/WS.h"

namespace Mona {


class WSUnmasking : public Decoding, public virtual Object {
public:
	WSUnmasking(Invoker& invoker, const Session& session, const UInt8* data,UInt32 size,UInt8 type) : _type(type), Decoding(invoker,session,"WSUnmasking",data,size) {}
	
private:
	bool					decode(Exception& ex, PacketReader& packet, UInt32 times);
	UInt8					_type;
};

inline bool WSUnmasking::decode(Exception& ex, PacketReader& packet, UInt32 times) {
	if (times)
		return false;
	WS::Unmask(packet);
	packet.reset(packet.position()-1);
	*(UInt8*)packet.current() = _type;
	return true;
}



} // namespace Mona
