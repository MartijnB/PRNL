<?php

/**
 * PRNL Library Header
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

define('DIR_SEP', DIRECTORY_SEPARATOR);

define('PRNL_VERSION', '0.1-dev');

define('__PRNL_ROOT', dirname(__FILE__));
define('__PRNL_ROOT_NETWORK', __PRNL_ROOT . DIR_SEP . 'network');
define('__PRNL_ROOT_PROT', __PRNL_ROOT . DIR_SEP . 'protocols');
define('__PRNL_ROOT_TOOLS', __PRNL_ROOT . DIR_SEP . 'tools');

//ip protocols
define('PROT_IPv4', 0);
define('PROT_IPv6', 41);

//content protocols
define('PROT_TCP', 6);
define('PROT_UDP', 17);

//this lib will only run on te console
if (php_sapi_name() != 'cli') {
	die('This script can only run from the commandline!'.PHP_EOL);
}

require_once(__PRNL_ROOT_TOOLS . DIR_SEP . 'ubyte.class.php');
require_once(__PRNL_ROOT_TOOLS . DIR_SEP . 'ushort.class.php');
require_once(__PRNL_ROOT_TOOLS . DIR_SEP . 'endian.class.php');
require_once(__PRNL_ROOT_TOOLS . DIR_SEP . 'memory.class.php');

require_once(__PRNL_ROOT_NETWORK . DIR_SEP . 'raw.network.class.php');
require_once(__PRNL_ROOT_NETWORK . DIR_SEP . 'raw.ip.network.class.php');

require_once(__PRNL_ROOT_NETWORK . DIR_SEP . 'raw.packet.class.php');

require_once(__PRNL_ROOT_PROT . DIR_SEP . 'completeable.protocol.interface.php');

require_once(__PRNL_ROOT_PROT . DIR_SEP . 'ipv4.interface.php');
require_once(__PRNL_ROOT_PROT . DIR_SEP . 'ipv4.protocol.class.php');

require_once(__PRNL_ROOT_PROT . DIR_SEP . 'tcp.interface.php');
require_once(__PRNL_ROOT_PROT . DIR_SEP . 'tcp.protocol.class.php');

require_once(__PRNL_ROOT_PROT . DIR_SEP . 'udp.interface.php');
require_once(__PRNL_ROOT_PROT . DIR_SEP . 'udp.protocol.class.php');