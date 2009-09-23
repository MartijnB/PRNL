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

class IPv4ProtocolPacket extends RawPacket {
	private $_data;
	
	public function __construct($data = '') {
		parent::__construct(IIPv4::HEADER_SIZE);
		
		if (strlen($data) > 0) {
			$this->setRawPacket($data);
		}
		else {
			$this->_buffer->setByte(IIPv4::VERSION_LENGTH, 69); //version & length
			$this->_buffer->setByte(IIPv4::TOS, 0); //tos
		
			$this->setTTL(64);
		}
	}
	
	//-- GETTERS
	public function getLength() {
		return $this->_buffer->getShort(IIPv4::LENGTH);
	}
	
	public function getIdSequence() {
		return $this->_buffer->getShort(IIPv4::ID_SEQ);
	}
	
	public function getOffset() {
		return $this->_buffer->getShort(IIPv4::OFFSET);
	}
	
	public function getTTL() {
		return $this->_buffer->getByte(IIPv4::TTL);
	}
	
	public function getProtocol() {
		return $this->_buffer->getByte(IIPv4::PROTOCOL);
	}
	
	public function getChecksum() {
		return $this->_buffer->getShort(IIPv4::CHECKSUM);
	}
	
	public function getSrcIP() {
		return long2ip($this->_buffer->getInteger(IIPv4::IP_SRC));
	}
	
	public function getDstIP() {
		return long2ip($this->_buffer->getInteger(IIPv4::IP_DST));
	}
	
	public function getRawData() {
		return $this->_buffer->getMemory(IIPv4::DATA);
	}
	
	/**
	 * Return the payload as a object
	 *
	 * @return RawPacket
	 */
	public function getDataObject() {
		if (!$this->_data) {
			if ($this->getProtocol() == PROT_UDP) {
				$this->_data = new UDPProtocolPacket($this->getRawData());
			}
			else {
				$this->_data = new RawPacket();
				$this->_data->setRawPacket($this->getRawData());
			}
		}
		
		return $this->_data;
	}
	//-- GETTERS
	
	//-- SETTERS
	public function setLength($length) {
		$this->_buffer->setShort(IIPv4::LENGTH, $length);
	}
	
	public function setIdSequence($idseq) {
		$this->_buffer->setShort(IIPv4::ID_SEQ, $idseq);
	}
	
	public function setOffset($offset) {
		$this->_buffer->setShort(IIPv4::OFFSET, $offset);
	}
	
	public function setTTL($ttl) {
		$this->_buffer->setByte(IIPv4::TTL, $ttl);
	}
	
	public function setProtocol($protocol) {
		$this->_buffer->setByte(IIPv4::PROTOCOL, $protocol);
	}
	
	public function setChecksum($checksum) {
		$this->_buffer->setShort(IIPv4::CHECKSUM, $checksum);
	}
	
	public function setSrcIP($ip) {
		if (!is_numeric($ip)) {
			$ip = ip2long($ip);
		}
		
		if ($ip === false)
			throw new Exception('Invalid src IP!');
			
		$this->_buffer->setInteger(IIPv4::IP_SRC, $ip);
	}
	
	public function setDstIP($ip) {
		if (!is_numeric($ip)) {
			$ip = ip2long($ip);
		}
		
		if ($ip === false)
			throw new Exception('Invalid dst IP!');
			
		$this->_buffer->setInteger(IIPv4::IP_DST, $ip);
	}
	
	public function setData(RawPacket $data) {
		$this->_data = $data;
		
		$this->_buffer->setMemorySize(IIPv4::HEADER_SIZE);
		$this->_buffer->addString($data->getRawPacket());
	}
	//-- SETTERS
	
	public function resetChecksum() {
		$this->_buffer->setShort(IIPv4::CHECKSUM, 0x0000);
	}
	
	/**
	 * Calculate the checksum of the packet
	 * 
	 */
	public function calculateChecksum() {
		$sum = new UShort();

		$length = IIPv4::HEADER_SIZE;
		$this->_buffer->resetReadPointer();
		while ($length > 1) {
			$sum->add($this->_buffer->readShort());
			$length -= 2;
		}
		$sum->bitNot();
		$this->_buffer->setShort(IIPv4::CHECKSUM, $sum->getValue());
	}
	
	public function completePacket() {
		if ($this->getLength() == 0)
			$this->setLength($this->getPacketLength());
			
		if ($this->getChecksum() == 0)
			$this->calculateChecksum();
			
		//hook the sub package
		if ($this->getProtocol() == PROT_UDP) {
			$this->_data->completePacket($this);
		}
		
		$this->_buffer->setMemorySize(IIPv4::HEADER_SIZE);
		$this->_buffer->addString($this->_data->getRawPacket());
	}
}

?>
