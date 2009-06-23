<?php

/**
 * UDP interface
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

interface IUDP {
	const HEADER_SIZE    = 0x08;
	
	const PORT_SRC       = 0x00; // 16 bit
	const PORT_DST       = 0x02; // 16 bit
	const LENGTH         = 0x04; // 16 bit
	const CHECKSUM       = 0x06; // 16 bit
	const DATA           = 0x08;
}

?>
