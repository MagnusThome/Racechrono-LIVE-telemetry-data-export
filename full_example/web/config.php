<?php
/**
 * Your settings for update.php and index.php.
 */

// Shared secret. The device sends it with every lap (uploadKey in arduino/config.h) and
// the Clear log button asks for it. Letters and digits only.
$secretKey = 'xxxxxxxxxxxxxxxx';

// Timezone for the timestamps in the log, so they show local wall clock time
// instead of the server's default (often UTC). See
// https://www.php.net/manual/en/timezones.php
$timezone = 'Europe/Stockholm';
