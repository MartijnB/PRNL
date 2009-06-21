<?php

/**
 * Raw IP Network Class
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

class RawIPNetwork extends RawNetwork  {
	private $_ipProtocol;
	private $_contentProtocol;
	
	public function createIPSocket($ipProtocol, $contentProtocol) {
		if ($ipProtocol == PROT_IPv4)
			$socketFamiliy = AF_INET;
		else if ($ipProtocol == PROT_IPv6)
			$socketFamiliy = AF_INET6;
		else
			new Exception("Unknown IP protocol");
		
		parent::createRawSocket($socketFamiliy, SOCK_RAW, $contentProtocol);
		
		//force own IP header
		socket_setopt($this->_socket, $this->_ipProtocol, 3, 1); //IP_HDRINCL = 3
		
		$this->_ipProtocol = $ipProtocol;
		$this->_contentProtocol = $contentProtocol;
	}
	
	public function readPacket($length = 16384) {
		$pData = parent::readPacket($length);
		
		if (!$pData)
			return false;
		
		return new IPv4ProtocolPacket($pData);
	}
	
	/**
	 * Send a raw packet through the socket
	 *
	 * @param IPv4ProtocolPacket $packet
	 */
	public function sendPacket(IPv4ProtocolPacket $packet) {
		parent::sendPacket($packet->getPacket());
	}
}

?>