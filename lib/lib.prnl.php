<?php

/**
 * PRNL Library Header
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

define('DIR_SEP', DIRECTORY_SEPARATOR);

define('__PRNL_ROOT', dirname(__FILE__));
define('__PRNL_ROOT_NETWORK', __PRNL_ROOT . DIR_SEP . 'network');
define('__PRNL_ROOT_PROT', __PRNL_ROOT . DIR_SEP . 'protocols');
define('__PRNL_ROOT_TOOLS', __PRNL_ROOT . DIR_SEP . 'tools');

require_once(__PRNL_ROOT_TOOLS . DIR_SEP . 'memory.class.php');
require_once(__PRNL_ROOT_TOOLS . DIR_SEP . 'endian.class.php');