<?php

/**
 * TCP Protocol Packet Class
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

class TCPProtocolPacket extends RawPacket implements ICompleteableProtocolPacket {
	public function __construct($data = '') {
		parent::__construct(ITCP::HEADER_SIZE);
		
		if (strlen($data) > 0) {
			$this->setRawPacket($data);
		}
		
		$this->setSegmentOffset(0x05);
	}
	
	//-- GETTERS
	public function getSrcPort() {
		return $this->_buffer->getShort(ITCP::PORT_SRC);
	}
	
	public function getDstPort() {
		return $this->_buffer->getShort(ITCP::PORT_DST);
	}
	
	public function getIdSequence() {
		return $this->_buffer->getInteger(ITCP::ID_SEQ);
	}
	
	public function getAckIdSequence() {
		return $this->_buffer->getInteger(ITCP::ACK_ID_SEQ);
	}
	
	public function getSegmentOffset() {
		return $this->_buffer->getByte(ITCP::SEG_OFF);
	}
	
	public function getFlags() {
		return $this->_buffer->getByte(ITCP::FLAGS);
	}
	
	public function getWindowSize() {
		return $this->_buffer->getShort(ITCP::WINDOW);
	}
	
	public function getChecksum() {
		return $this->_buffer->getShort(ITCP::CHECKSUM);
	}
	
	public function getUrgentPointer() {
		return $this->_buffer->getShort(ITCP::URGENT);
	}
		
	public function getData() {
		return $this->_buffer->getMemory(ITCP::DATA);
	}
	//-- GETTERS
	
	//-- SETTERS
	public function setSrcPort($port) {
		$this->_buffer->setShort(ITCP::PORT_SRC, $port);
	}
	
	public function setDstPort($port) {
		$this->_buffer->setShort(ITCP::PORT_DST, $port);
	}
	
	public function setIdSequence($id_seq) {
		$this->_buffer->setInteger(ITCP::ID_SEQ, $id_seq);
	}
	
	public function setAckIdSequence($id_seq) {
		$this->_buffer->setInteger(ITCP::ACK_ID_SEQ, $id_seq);
	}
	
	public function setSegmentOffset($off) {
		$this->_buffer->setByte(ITCP::SEG_OFF, $off << 4);
	}
	
	public function setFlags($flags) {
		$this->_buffer->setByte(ITCP::FLAGS, $flags);
	}
	
	public function setWindowSize($size) {
		$this->_buffer->setShort(ITCP::WINDOW, $size);
	}
	
	public function setChecksum($checksum) {
		$this->_buffer->setShort(ITCP::CHECKSUM, $checksum);
	}
	
	public function setUrgentPointer($p) {
		$this->_buffer->setShort(ITCP::URGENT, $p);
	}
		
	public function setData($data) {
		$this->_buffer->setMemorySize(ITCP::HEADER_SIZE);
		$this->_buffer->addString($data);
	}
	//-- SETTERS
	
	public function resetChecksum() {
		$this->_buffer->setShort(ITCP::CHECKSUM, 0x0000);
	}
	
	/**
	 * Calculate the checksum of the packet
	 */
	public function calculateChecksum(Memory $ipPacketBuffer) {
		$this->_buffer->resetReadPointer();
		
		$sum = new UShort();
		//the data
		for ($i = 0; $i < $this->getPacketLength(); $i+=2) {
			$sum->add($this->_buffer->readShort());
		}
		
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_SRC));
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_SRC+2));
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_DST));
		$sum->add($ipPacketBuffer->getShort(IIPv4::IP_DST+2));
		$sum->add(PROT_TCP); //protocol
		$sum->add($this->getPacketLength());
		
		$sum->bitNot();
		$this->_buffer->setShort(IUDP::CHECKSUM, $sum->getValue());
	}
	
	public function completePacket(Memory $ipPacketBuffer) {
		if ($this->getChecksum() == 0)
			$this->calculateChecksum($ipPacketBuffer);
	}
}