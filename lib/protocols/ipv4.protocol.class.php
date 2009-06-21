<?php

/**
 * IPv4 Protocol Packet Class
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

class IPv4ProtocolPacket extends RawPacket implements IIPv4 {
	public function __construct($data = '') {
		parent::__construct(HEADER_SIZE);
		
		if (strlen($data) > 0)
			$this->setRawPacket($data);
		
		$this->_buffer->setByte(VERSION_LENGTH, 69); //version & length
		$this->_buffer->setByte(TOS, 0); //tos
		
		$this->setTTL(64);
	}
	
	//-- GETTERS
	public function getLength() {
		return $this->_buffer->getShort(LENGTH);
	}
	
	public function getIdSequence() {
		return $this->_buffer->getShort(ID_SEQ);
	}
	
	public function getOffset() {
		return $this->_buffer->getShort(OFFSET);
	}
	
	public function getTTL() {
		return $this->_buffer->getByte(TTL);
	}
	
	public function getProtocol() {
		return $this->_buffer->getByte(PROTOCOL);
	}
	
	public function getChecksum() {
		return $this->_buffer->getShort(CHECKSUM);
	}
	
	public function getSrcIP() {
		return long2ip($this->_buffer->getInteger(SRC_IP));
	}
	
	public function getDstIP() {
		return long2ip($this->_buffer->getInteger(DST_IP));
	}
	
	public function getData() {
		return $this->_buffer->getMemory(DATA);
	}
	//-- GETTERS
	
	//-- SETTERS
	public function setLength($length) {
		$this->_buffer->setShort(LENGTH, $length);
	}
	
	public function setIdSequence($idseq) {
		$this->_buffer->setShort(ID_SEQ, $idseq);
	}
	
	public function setOffset($offset) {
		$this->_buffer->setShort(OFFSET, $offset);
	}
	
	public function setTTL($ttl) {
		$this->_buffer->setByte(TTL, $offset);
	}
	
	public function setProtocol($protocol) {
		$this->_buffer->setByte(PROTOCOL, $protocol);
	}
	
	public function setChecksum($checksum) {
		$this->_buffer->setShort(CHECKSUM, $checksum);
	}
	
	public function setSrcIP($ip) {
		if (!is_numeric($ip)) {
			$ip = ip2long($ip);
		}
		
		if ($ip === false)
			throw new Exception('Invalid src IP!');
			
		$this->_buffer->setInteger(SRC_IP, $ip);
	}
	
	public function setDstIP($ip) {
		if (!is_numeric($ip)) {
			$ip = ip2long($ip);
		}
		
		if ($ip === false)
			throw new Exception('Invalid dst IP!');
			
		$this->_buffer->setInteger(DST_IP, $ip);
	}
	
	public function setData($data) {
		$this->_buffer->setMemorySize(HEADER_SIZE);
		$this->_buffer->addString($data);
	}
	//-- SETTERS
	
	public function resetChecksum() {
		$this->_buffer->setShort(CHECKSUM, 0x0000);
	}
	
	/**
	 * Calculate the checksum of the packet
	 * 
	 * @todo implement
	 */
	public function calculateChecksum() {
		throw new Exception('Not yet implemented');
	}
	
	public function getPacket() {
		if ($this->getLength() == 0)
			$this->setLength($this->getPacketLength());
			
		if ($this->getChecksum() == 0)
			$this->calculateChecksum();
		
		return $this->_buffer->getMemory();
	}
}

?>