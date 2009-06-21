<?php

/**
 * Byte Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class Byte {
	private $_value;
	
	public function __construct($value = 0) {
		$this->add($value);
	}
	
	public function add($value) {
		if ($value > 255) {
			$value -= 256;
			$this->add($value);
			return;
		}
		
		if (($this->_value + $value) > 255) {
			$this->_value = ($this->_value + $value) - 256;
		}
		else {
			$this->_value += $value;
		}
	}
	
	public function distract($value) {
		if ($value > 255) {
			$value -= 256;
			$this->distract($value);
			return;
		}
		
		if (($this->_value - $value) < 0) {
			$this->_value = ($this->_value - $value) + 256;
		}
		else {
			$this->_value -= $value;
		}
	}
	
	
	public function getValue() {
		return $this->_value;
	}
	
	public function __toString() {
		return (string)$this->getValue();
	}
}

?>