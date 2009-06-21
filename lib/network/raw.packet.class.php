<?php

/**
 * Raw Packet Class
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

class RawPacket implements IPacket {
	protected $_buffer;
	
	public function __construct($packetSize = 0) {
		$this->_buffer = new Memory($packetSize);
	}
	
	public function getRawPacket() {
		return $this->_buffer->getMemory();
	}
	
	public function setRawPacket($data) {
		$this->_buffer->addString($data);
	}
	
	public function getLength() {
		return $this->_buffer->getMemoryLength();
	}
	
	public function dumpPacket() {
		$this->_buffer->dumpMemory();
	}
}

?>