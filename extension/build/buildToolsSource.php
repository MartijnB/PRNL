<?php

$files = array(
			   '../../lib/tools/endian.class.php',
			   '../../lib/tools/memory.class.php',
			   '../../lib/tools/ubyte.class.php',
			   '../../lib/tools/ushort.class.php',
			  );
			  
$source = '<?php '.PHP_EOL;
foreach ($files as $file) {
	$source .= getCleanFileSource($file).PHP_EOL;
}

file_put_contents('tools.source.php', $source);

function getCleanFileSource($file) {
	//$source = php_strip_whitespace($file);
	$source = file_get_contents($file);
	$source = trim($source);
	
	//remove tags
	$source = trimString($source, '<?php');
	$source = trimString($source, '<?');
	$source = trimString($source, '?>');
	
	return $source;
}

//http://nl.php.net/manual/en/function.trim.php#89751
function trimString($input, $string){
        $input = trim($input);
        $string = str_replace("\\", "\\\\", $string);
        $string = str_replace('/', '\\/', $string);
        $string = str_replace('?', '\\?', $string);
        $startPattern = "/^($string)+/i";
        $endPattern = "/($string)+$/i";
        return trim(preg_replace($endPattern, '', preg_replace($startPattern,'',$input)));
} 