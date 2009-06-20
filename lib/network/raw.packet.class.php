<?php

/**
 * Raw Packet Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class RawPacket implements IPacket {
	private $_buffer;
	
	public function __construct() {
		$this->_buffer = new Memory();
	}
	
	public function getRawPacket() {
		return $this->_buffer->getMemory();
	}
	
	public function setRawPacket($data) {
		$this->_buffer->addString($data);
	}
}

?>