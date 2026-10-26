<?php
/**
 * Viewer for datalog.csv written by update.php.
 *
 * The logger sends one row per completed lap, so every row is a finished lap
 * (older rows may instead be temperature-only samples from an earlier
 * version). Columns, no header row in the file:
 *
 *   0: timestamp "Ymd H:i:s" (written by update.php, in $timezone from config.php)
 *   1: engine_oil_temp
 *   2: coolant_temp
 *   3: previous lap number   (last completed lap)
 *   4: previous lap time     (last completed lap, seconds)
 *   5: test                  (1 for a lap made by the device's test mode)
 *
 * Older rows may have fewer columns; they are padded so everything lines up.
 */

require __DIR__ . '/config.php';

$csvFile        = $logFile;   // in $dataDir, see config.php
$refreshSeconds = 5;   // set to 0 to disable auto-refresh
$columnCount    = 6;

// ---------------------------------------------------------------------------
// Actions. Handled before any output, then redirected back to a clean GET so
// that a browser refresh can never repeat the delete.
// ---------------------------------------------------------------------------
$action = $_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action'])
    ? (string) $_POST['action']
    : '';

if ($action === 'clear') {
    $confirmed = isset($_POST['confirm']) && $_POST['confirm'] === 'yes';
    $keyOk     = $confirmed && key_is_valid(isset($_POST['key']) ? (string) $_POST['key'] : '');
    $ok        = false;
    if ($keyOk) {
        // Truncates in place (creating the file if absent) under an exclusive lock.
        // The error is suppressed: a PHP warning here would be printed before the
        // redirect header and turn the 303 into a "headers already sent" 200.
        $ok = @file_put_contents($csvFile, '', LOCK_EX) !== false;
        if (!$ok) {
            error_log('index.php: could not clear ' . $csvFile);
        }
    }
    if ($confirmed && !$keyOk) {
        // Back to the confirmation, so the key can be typed again.
        header('Location: ' . basename(__FILE__) . '?confirm=1&cleared=key', true, 303);
    } else {
        header('Location: ' . basename(__FILE__) . '?cleared=' . ($ok ? '1' : '0'), true, 303);
    }
    exit;
}

$confirmClear = isset($_GET['confirm']) && $_GET['confirm'] === '1';

// ---------------------------------------------------------------------------
// Read the log. Shared lock so we never read a row mid-write.
// ---------------------------------------------------------------------------
$rows = [];
if (is_file($csvFile) && ($fp = fopen($csvFile, 'r')) !== false) {
    if (flock($fp, LOCK_SH)) {
        while (($row = fgetcsv($fp, 0, ',', '"', '\\')) !== false) {
            if (count($row) < 1 || ($row[0] === null && count($row) === 1)) {
                continue;
            }
            $rows[] = array_pad($row, $columnCount, '');
        }
        flock($fp, LOCK_UN);
    }
    fclose($fp);
}

$total  = count($rows);
$latest = $total > 0 ? $rows[$total - 1] : null;

$lastUpdate = $latest !== null ? parse_log_time($latest[0]) : null;

/** The log stores "Ymd H:i:s"; turn that into a DateTime (null if unparseable). */
function parse_log_time(string $raw): ?DateTime
{
    $raw = trim($raw);
    if ($raw === '') {
        return null;
    }
    $dt = DateTime::createFromFormat('Ymd H:i:s', $raw);

    // createFromFormat() accepts trailing garbage; reject it explicitly.
    return ($dt instanceof DateTime && $dt->format('Ymd H:i:s') === $raw) ? $dt : null;
}

/** Display form: yyyy-mm-dd hh:mm:ss, falling back to the raw text. */
function fmt_time(string $raw): string
{
    $dt = parse_log_time($raw);

    return $dt !== null ? $dt->format('Y-m-d H:i:s') : trim($raw);
}

/** Seconds to m:ss.hh. */
function fmt_lap_time(string $raw): string
{
    $raw = trim($raw);
    if ($raw === '' || !is_numeric($raw)) {
        return $raw;
    }
    $total   = (float) $raw;
    $minutes = floor($total / 60);
    $seconds = $total - ($minutes * 60);

    return sprintf('%d:%05.2f', $minutes, $seconds);
}

/** Format one numeric cell, passing through anything non-numeric as-is. */
function fmt_cell(string $value, int $decimals = 0): string
{
    $value = trim($value);
    if ($value === '') {
        return '—';
    }
    if (!is_numeric($value)) {
        return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
    }

    return number_format((float) $value, $decimals, '.', '');
}

/** Like fmt_cell(), but an empty value stays empty instead of showing a dash. */
function fmt_optional(string $value, int $decimals = 0): string
{
    $value = trim($value);
    if ($value === '') {
        return '';
    }
    if (!is_numeric($value)) {
        return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
    }

    return number_format((float) $value, $decimals, '.', '');
}

function e(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
}

/** True for a lap made by the device's test mode rather than RaceChrono. */
function is_test_row(array $row): bool
{
    return trim($row[5]) === '1';
}

// ---------------------------------------------------------------------------
// Laps. Each row is one completed lap, so there is nothing to collapse. Rows
// from before the logger was changed to lap-only still have empty lap fields;
// those are counted separately and shown as a dash. Runs in log order, so the
// last lap found is the newest one.
// ---------------------------------------------------------------------------
$lapCount = 0;      // rows that actually carry a completed lap
$lastLap  = null;   // ['number' => string, 'time' => string]

foreach ($rows as $row) {
    $number = trim($row[3]);
    if ($number === '') {
        continue;
    }
    $lapCount++;
    if (trim($row[4]) !== '') {
        $lastLap = ['number' => $number, 'time' => trim($row[4]), 'test' => is_test_row($row)];
    }
}

$displayRows = array_reverse($rows, true);   // newest first, original indexes kept

$banner = '';
$bannerClass = 'ok';
if (isset($_GET['cleared'])) {
    if ($_GET['cleared'] === '1') {
        $banner = 'Log cleared.';
    } elseif ($_GET['cleared'] === 'key') {
        $banner      = 'Wrong key, the log was not cleared.';
        $bannerClass = 'err';
    } else {
        $banner      = 'Could not clear the log file — check that $dataDir in config.php exists and is writable.';
        $bannerClass = 'err';
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<noscript><meta http-equiv="refresh" content="<?= (int) $refreshSeconds ?>"></noscript>
<title>RaceChrono lap telemetry</title>
<style>
    :root {
        --bg: #12151a;
        --panel: #1a1f27;
        --line: #2b323d;
        --text: #e8edf3;
        --muted: #8b98a9;
        --accent: #4da3ff;
        --ok: #34c759;
        --warn: #ff9f0a;
        --err: #ff453a;
    }
    * { box-sizing: border-box; }
    body {
        margin: 0;
        padding: 24px;
        background: var(--bg);
        color: var(--text);
        font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    }
    h1 {
        font-size: 1.35rem;
        font-weight: 600;
        margin: 0 0 10px;
        letter-spacing: .01em;
    }
    .bar {
        display: flex;
        flex-wrap: wrap;
        gap: 16px;
        align-items: center;
        justify-content: space-between;
        margin-bottom: 14px;
    }
    .status {
        display: flex;
        flex-wrap: wrap;
        gap: 16px;
        align-items: center;
        color: var(--muted);
        font-size: .85rem;
    }
    .lapchip {
        color: var(--accent);
        font-weight: 600;
        font-variant-numeric: tabular-nums;
    }
    .actions { display: flex; gap: 8px; align-items: center; }
    .tag {
        display: inline-block;
        margin-left: 6px;
        padding: 1px 6px;
        border-radius: 4px;
        border: 1px solid rgba(255, 159, 10, .45);
        background: rgba(255, 159, 10, .14);
        color: var(--warn);
        font-size: .7rem;
        font-weight: 600;
        letter-spacing: .05em;
        text-transform: uppercase;
        vertical-align: middle;
    }
    .dot {
        display: inline-block;
        width: 8px;
        height: 8px;
        border-radius: 50%;
        margin-right: 6px;
        vertical-align: middle;
    }
    .dot.ok   { background: var(--ok); box-shadow: 0 0 8px var(--ok); }
    .dot.err  { background: var(--err); }
    .btn {
        appearance: none;
        border: 1px solid var(--line);
        background: #222834;
        color: var(--text);
        border-radius: 7px;
        padding: 7px 13px;
        font: inherit;
        font-size: .85rem;
        line-height: 1.2;
        cursor: pointer;
        text-decoration: none;
        white-space: nowrap;
        display: inline-block;
    }
    .btn:hover { border-color: #3b4655; background: #28303d; }
    .btn.danger {
        background: rgba(255, 69, 58, .14);
        border-color: rgba(255, 69, 58, .45);
        color: #ff8a80;
    }
    .btn.danger:hover { background: rgba(255, 69, 58, .24); border-color: rgba(255, 69, 58, .7); }
    .btn.off { opacity: .4; pointer-events: none; }
    .confirm { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin: 0; }
    .confirm .q { color: var(--warn); font-size: .85rem; }
    .confirm input {
        border: 1px solid var(--line);
        background: var(--bg);
        color: var(--text);
        border-radius: 7px;
        padding: 7px 10px;
        font: inherit;
        font-size: .85rem;
        width: 11em;
    }
    .banner {
        border: 1px solid transparent;
        border-radius: 8px;
        padding: 9px 14px;
        font-size: .85rem;
        margin-bottom: 14px;
    }
    .banner.ok  { background: rgba(52, 199, 89, .12); border-color: rgba(52, 199, 89, .35); color: #7ee794; }
    .banner.err { background: rgba(255, 69, 58, .12); border-color: rgba(255, 69, 58, .35); color: #ff8a80; }
    .table-wrap {
        background: var(--panel);
        border: 1px solid var(--line);
        border-radius: 10px;
        overflow: auto;
        max-height: calc(100vh - 190px);
    }
    table {
        width: 100%;
        border-collapse: collapse;
        font-variant-numeric: tabular-nums;
    }
    thead th {
        position: sticky;
        top: 0;
        background: #222834;
        text-align: right;
        font-size: .75rem;
        text-transform: uppercase;
        letter-spacing: .06em;
        color: var(--muted);
        font-weight: 600;
        padding: 10px 16px;
        border-bottom: 1px solid var(--line);
        white-space: nowrap;
    }
    tbody td {
        padding: 8px 16px;
        border-bottom: 1px solid rgba(43, 50, 61, .6);
        font-size: .9rem;
        white-space: nowrap;
        text-align: right;
    }
    tbody tr:nth-child(even) { background: rgba(255, 255, 255, .015); }
    tbody tr:hover { background: rgba(77, 163, 255, .07); }
    tbody tr:first-child td { color: var(--accent); font-weight: 600; }
    .empty {
        padding: 48px 16px;
        text-align: center;
        color: var(--muted);
    }
    .empty code {
        background: #222834;
        padding: 2px 6px;
        border-radius: 4px;
        font-size: .85em;
    }
</style>
</head>
<body>

<h1>RaceChrono lap telemetry</h1>

<div id="content">

<?php if ($banner !== ''): ?>
    <div class="banner <?= $bannerClass ?>"><?= e($banner) ?></div>
<?php endif; ?>

<div class="bar">
    <div class="status">
<?php if ($latest === null): ?>
        <span><span class="dot err"></span>No laps logged yet</span>
<?php else: ?>
        <span><span class="dot ok"></span><?= $lapCount ?> <?= $lapCount === 1 ? 'lap' : 'laps' ?> logged</span>
<?php endif; ?>
<?php if ($lastLap !== null): ?>
        <span class="lapchip">Last lap <?= e(fmt_optional($lastLap['number'], 0)) ?> · <?= e(fmt_lap_time($lastLap['time'])) ?><?php if ($lastLap['test']): ?><span class="tag">test</span><?php endif; ?></span>
<?php endif; ?>
<?php if ($lastUpdate !== null): ?>
        <span>Last logged: <?= $lastUpdate->format('Y-m-d H:i:s') ?></span>
<?php endif; ?>
<?php if ($refreshSeconds > 0): ?>
        <span>Auto-refresh: <?= (int) $refreshSeconds ?>s</span>
<?php endif; ?>
    </div>

    <div class="actions">
<?php if ($confirmClear && $total > 0): ?>
        <form class="confirm" method="post" action="<?= e(basename(__FILE__)) ?>">
            <input type="hidden" name="action" value="clear">
            <input type="hidden" name="confirm" value="yes">
            <span class="q">Delete the entire log (<?= $total ?> <?= $total === 1 ? 'row' : 'rows' ?>)? Cannot be undone.</span>
            <input type="password" name="key" placeholder="Key" aria-label="Key" required autocomplete="off" autofocus>
            <button type="submit" class="btn danger">Yes, clear the log</button>
            <a class="btn" href="<?= e(basename(__FILE__)) ?>">Cancel</a>
        </form>
<?php elseif ($total > 0): ?>
        <a class="btn danger" href="<?= e(basename(__FILE__)) ?>?confirm=1">Clear log</a>
<?php else: ?>
        <span class="btn off">Clear log</span>
<?php endif; ?>
    </div>
</div>

<div class="table-wrap">
<?php if ($total === 0): ?>
    <div class="empty">
        Waiting for the first completed lap.<br><br>
        The ESP32 posts to <code>update.php</code> when a lap finishes, which appends one row to <code>datalog.csv</code>.
    </div>
<?php else: ?>
    <table>
        <thead>
            <tr>
                <th>Lap</th>
                <th>Lap time</th>
                <th>Engine oil temp</th>
                <th>Coolant temp</th>
                <th>Time</th>
            </tr>
        </thead>
        <tbody>
<?php foreach ($displayRows as $index => $row): ?>
            <tr>
                <td><?php if (is_test_row($row)): ?><span class="tag">test</span> <?php endif; ?><?= fmt_cell($row[3]) ?></td>
                <td><?= trim($row[4]) === '' ? '—' : e(fmt_lap_time($row[4])) ?></td>
                <td><?= fmt_cell($row[1]) ?></td>
                <td><?= fmt_cell($row[2]) ?></td>
                <td><?= e(fmt_time($row[0])) ?></td>
            </tr>
<?php endforeach; ?>
        </tbody>
    </table>
<?php endif; ?>
</div>

</div><!-- /#content -->

<script>
(function () {
    var REFRESH_MS = <?= (int) $refreshSeconds * 1000 ?>;
    var content = document.getElementById('content');
    if (!REFRESH_MS || !content || !window.fetch) {
        return;
    }

    // Drop ?cleared=... from the address bar so the banner doesn't reappear on
    // refresh. ?confirm=1 has to stay: the refresh fetches this same address,
    // and without it the confirmation would be swapped back to the button.
    if (window.history && window.history.replaceState && window.URLSearchParams && window.location.search) {
        var params = new URLSearchParams(window.location.search);
        if (params.has('cleared')) {
            params.delete('cleared');
            var query = params.toString();
            window.history.replaceState(null, '', window.location.pathname + (query ? '?' + query : ''));
        }
    }

    // No refresh while the clear confirmation is up, so a half typed key is
    // not wiped.
    function busy() {
        return !!content.querySelector('form.confirm');
    }

    window.setInterval(function () {
        if (document.hidden || busy()) {
            return;
        }
        fetch(window.location.href, { cache: 'no-store' })
            .then(function (r) {
                return r.ok ? r.text() : Promise.reject(new Error('HTTP ' + r.status));
            })
            .then(function (html) {
                var fresh = new DOMParser().parseFromString(html, 'text/html').getElementById('content');
                if (fresh) {
                    content.innerHTML = fresh.innerHTML;
                }
            })
            .catch(function () { /* keep showing the last good render */ });
    }, REFRESH_MS);
})();
</script>

</body>
</html>
