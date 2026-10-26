<?php
/**
 * Settings shared by update.php and index.php.
 */

// Shared secret. The device sends it with every lap (uploadKey in main.ino) and
// the Clear log button asks for it. Letters and digits only. Nothing is
// accepted while it is still the placeholder.
$secretKey = 'xxxxxxxxxxxxxxxx';

// Server side folder for datalog.csv, as a path on the server's disk (not a
// URL). It must be writable by the web server, while the folder with the PHP
// files should not be. Best outside the web root, for example
// '/home/me/racechrono-data'; a subfolder here works too, but then the log can
// also be downloaded directly from the web. The folder must already exist.
$dataDir = __DIR__ . '/data';

// Timezone for the timestamps in the log, so they show local wall clock time
// instead of the server's default (often UTC). See
// https://www.php.net/manual/en/timezones.php
$timezone = 'Europe/Stockholm';

$logFile = rtrim($dataDir, '/\\') . '/datalog.csv';
date_default_timezone_set($timezone);

/** True when $given matches the key, and the key has actually been set. */
function key_is_valid(string $given): bool
{
    global $secretKey;

    if ($secretKey === '' || $secretKey === 'xxxxxxxxxxxxxxxx') {
        return false;
    }

    return hash_equals($secretKey, $given);
}
