<?php

/**
 * Endian Conversion Class
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

class Endian {
	public static function convertEndianShort($short) {
		$firstBit = $short >> 8;
		$short = $short - (($short >> 8) << 8);
		$secondBit = $short;
		
		return ($secondBit << 8) + $firstBit;
	}
	
	public static function convertEndianInteger($int) {
		$firstBit = ($int >> 24 & 0xff);
		$int = $int - (($int >> 24 & 0xff) << 24);
		$secondBit = ($int >> 16 & 0xff);
		$int = $int - (($int >> 16 & 0xff ) << 16);
		$tirthBit = ($int >> 8 & 0xff);
		$fourthBit = $int - (($int >> 8 & 0xff) << 8);
		
		return ($fourthBit << 24) +($tirthBit << 16) + ($secondBit << 8) + $firstBit;
	}
}

?>