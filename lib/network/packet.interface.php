<?php

/**
 * Packet interface
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

interface IPacket {
	public function getRawPacket();
	public function setRawPacket();
}

?>