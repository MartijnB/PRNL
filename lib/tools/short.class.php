<?php

/**
 * Short Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class Short {
	private $_value;
	
	public function __construct($value = 0) {
		$this->add($value);
	}
	
	public function add($value) {
		if ($value > 65535) {
			$value -= 65536;
			$this->add($value);
			return;
		}
		
		if (($this->_value + $value) > 65535) {
			$this->_value = ($this->_value + $value) - 65536;
		}
		else {
			$this->_value += $value;
		}
	}
	
	public function distract($value) {
		if ($value > 65535) {
			$value -= 65536;
			$this->distract($value);
			return;
		}
		
		if (($this->_value - $value) < 0) {
			$this->_value = ($this->_value - $value) + 65536;
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