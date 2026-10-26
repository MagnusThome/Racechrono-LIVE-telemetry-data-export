<?php
/**
 * Receives one completed lap from the ESP32 and appends it to datalog.csv in
 * $dataDir (config.php).
 *
 * The device calls this once per finished lap, so each row is one lap.
 *
 * Query parameters:
 *   key (required, must match $secretKey in config.php)
 *   engine_oil_temp, coolant_temp, prev_lap_number, prev_lap_time (optional)
 *   test (optional, 1 for a lap made by the device's test mode)
 *
 * Column order is fixed, so every row gets the same number of fields even when
 * a sensor (or the lap timer) has nothing to report yet. Empty means "no value".
 *
 * Anything but a 200 makes the device keep the lap and send it again later.
 */

require __DIR__ . '/config.php';

if (!key_is_valid(isset($_GET['key']) ? (string) $_GET['key'] : '')) {
    http_response_code(403);
    echo "bad key";
    exit;
}

$columns = ['engine_oil_temp', 'coolant_temp', 'prev_lap_number', 'prev_lap_time', 'test'];

// The timestamp is in $timezone, set in config.php.
$row      = [date('Ymd H:i:s')];
$hasValue = false;

foreach ($columns as $name) {
    $value = isset($_GET[$name]) ? trim((string) $_GET[$name]) : '';

    // Only plain decimal numbers, so nothing else can end up in the log.
    if ($value !== '' && preg_match('/^-?[0-9]+(\.[0-9]+)?$/', $value)) {
        $row[]    = $value;
        $hasValue = true;
    } else {
        $row[] = '';
    }
}

if (!$hasValue) {
    http_response_code(400);
    echo "no values";
    exit;
}

// A failed write is reported as an error, so the device keeps the lap instead
// of believing it was stored.
// The server path only goes to the server's error log, not to the client.
$fp = @fopen($logFile, 'a');
if ($fp === false || !flock($fp, LOCK_EX)) {
    error_log('update.php: could not write ' . $logFile . ', check that $dataDir exists and is writable');
    http_response_code(500);
    echo "could not write datalog.csv";
    exit;
}
// The escape parameter is passed explicitly: leaving it out is deprecated
// as of PHP 8.4, and a notice here would be sent to the device.
$written = fputcsv($fp, $row, ',', '"', '\\');
fflush($fp);
flock($fp, LOCK_UN);
fclose($fp);

if ($written === false) {
    http_response_code(500);
    echo "could not write datalog.csv";
    exit;
}

echo "OK";
