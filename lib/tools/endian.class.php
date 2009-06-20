<?php

/**
 * Endian Conversion Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
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