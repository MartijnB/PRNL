<?php

/**
 * IPv4 interface
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

interface IIPv4 {
	const HEADER_SIZE    = 0x14;
	
	const VERSION_LENGTH = 0x00; // 8 bit
	const TOS            = 0x01; // 8 bit
	const LENGTH         = 0x02; // 16 bit
	const ID_SEQ         = 0x04; // 16 bit
	const OFFSET         = 0x06; // 16 bit
	const TTL            = 0x08; // 8 bit
	const PROTOCOL       = 0x09; // 8 bit
	const CHECKSUM       = 0x0A; // 16 bit
	const IP_SRC         = 0x0C; // 32 bit
	const IP_DST         = 0x10; // 32 bit
	const DATA           = 0x14;
}

?>