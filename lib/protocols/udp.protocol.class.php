<?php

/**
 * UDP Protocol Packet Class
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

class UDPProtocolPacket extends RawPacket implements ICompleteableProtocolPacket {
	public function __construct($data = '') {
		parent::__construct(IUDP::HEADER_SIZE);
		
		if (strlen($data) > 0) {
			$this->setRawPacket($data);
		}
	}
	
	//-- GETTERS
	public function getSrcPort() {
		return $this->_buffer->getShort(IUDP::PORT_DST);
	}
	
	public function getDstPort() {
		return $this->_buffer->getShort(IUDP::PORT_SRC);
	}
	
	public function getLength() {
		return $this->_buffer->getShort(IUDP::LENGTH);
	}
	
	public function getChecksum() {
		return $this->_buffer->getShort(IUDP::CHECKSUM);
	}
		
	public function getData() {
		return $this->_buffer->getMemory(IUDP::DATA);
	}
	//-- GETTERS
	
	//-- SETTERS
	public function setSrcPort($port) {
		$this->_buffer->setShort(IUDP::PORT_SRC, $port);
	}
	
	public function setDstPort($port) {
		$this->_buffer->setShort(IUDP::PORT_DST, $port);
	}
	
	public function setLength($length) {
		$this->_buffer->setShort(IUDP::LENGTH, $length);
	}
	
	public function setChecksum($checksum) {
		$this->_buffer->setShort(IUDP::CHECKSUM, $checksum);
	}
		
	public function setData($data) {
		$this->_buffer->setMemorySize(IUDP::HEADER_SIZE);
		$this->_buffer->addString($data);
	}
	//-- SETTERS
	
	public function resetChecksum() {
		$this->_buffer->setShort(IUDP::CHECKSUM, 0x0000);
	}
	
	/**
	 * Calculate the checksum of the packet
	 */
	public function calculateChecksum(Memory $ipPacketBuffer) {
		$this->_buffer->resetReadPointer();
		
		$sum = new UShort();
		//the data
		for ($i = 0; $i < $this->getLength(); $i+=2) {
			$sum->add($this->_buffer->readShort());
		}
		
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_SRC));
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_SRC+2));
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_DST));
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_DST+2));
		$sum->add(17); //protocol
		$sum->add($this->getLength());
		
		$sum->bitNot();
		$this->_buffer->setShort(IUDP::CHECKSUM, $sum->getValue());
	}
	
	public function completePacket(Memory $ipPacketBuffer) {
		if ($this->getLength() == 0)
			$this->setLength($this->getPacketLength());
			
		if ($this->getChecksum() == 0)
			$this->calculateChecksum($ipPacketBuffer);
	}
}

?>
