<?php

/**
 * TCP interface
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

interface ITCP {
	const HEADER_SIZE    = 0x14;
	
	const PORT_SRC       = 0x00; // 16 bit
	const PORT_DST       = 0x02; // 16 bit
	const ID_SEQ         = 0x04; // 32 bit
	const ACK_ID_SEQ     = 0x08; // 32 bit
	const SEG_OFF        = 0x0C; //  8 bit
	const FLAGS          = 0x0D; //  8 bit
	const WINDOW         = 0x0E; // 16 bit
	const CHECKSUM       = 0x10; // 16 bit
	const URGENT         = 0x12; // 16 bit
	const DATA           = 0x14;
	
	const FLAG_FIN       = 0x01;
	const FLAG_SYN       = 0x02;
	const FLAG_RST       = 0x04;
	const FLAG_PUSH      = 0x08;
	const FLAG_ACK       = 0x10;
	const FLAG_URG       = 0x20;
}

