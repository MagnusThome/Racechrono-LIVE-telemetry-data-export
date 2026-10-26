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
 *   6: fuel_level            (tank level in %, 0-100)
 *
 * Older rows may have fewer columns; they are padded so everything lines up.
 */

require __DIR__ . '/config.php';

$logFile = __DIR__ . '/data/datalog.csv';
date_default_timezone_set($timezone);

$csvFile        = $logFile;
$refreshSeconds = 5;   // set to 0 to disable auto-refresh
$columnCount    = 7;

// ---------------------------------------------------------------------------
// Actions. Handled before any output, then redirected back to a clean GET so
// that a browser refresh can never repeat the delete.
// ---------------------------------------------------------------------------
$action = $_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action'])
    ? (string) $_POST['action']
    : '';

if ($action === 'clear') {
    $confirmed = isset($_POST['confirm']) && $_POST['confirm'] === 'yes';
    $keyOk     = $confirmed && hash_equals($secretKey, isset($_POST['key']) ? (string) $_POST['key'] : '');
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

$total = count($rows);

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

/**
 * A temperature with its unit, written the SI way: number, non-breaking space,
 * then "°C" (degree sign + C, rather than the single ℃ character, which
 * Unicode keeps only for compatibility). A missing value stays a plain dash.
 */
function fmt_temp(string $value): string
{
    $cell = fmt_cell($value);

    return is_numeric(trim($value)) ? $cell . '&nbsp;°C' : $cell;
}

/** Fuel tank level as a whole percentage, "45 %"; a missing value stays a dash. */
function fmt_percent(string $value): string
{
    $cell = fmt_cell($value);

    return is_numeric(trim($value)) ? $cell . '&nbsp;%' : $cell;
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

// Each row is one completed lap. Rows from before the logger was changed to
// lap-only have empty lap fields and are shown with a dash.
$displayRows = array_reverse($rows, true);   // newest first, original indexes kept

// The measured values on each card. One that is empty in every lap in the log,
// for example on a car that never reports its oil temperature, is left out of
// all cards; one that is only missing now and then shows a dash, so the cards
// don't change shape from lap to lap.
$fields = [
    'oil'     => ['col' => 1, 'label' => 'Oil',     'head' => 'Engine oil temp', 'fmt' => 'fmt_temp'],
    'coolant' => ['col' => 2, 'label' => 'Coolant', 'head' => 'Coolant temp',    'fmt' => 'fmt_temp'],
    'fuel'    => ['col' => 6, 'label' => 'Fuel',    'head' => 'Fuel',            'fmt' => 'fmt_percent'],
];
$fields = array_filter($fields, function (array $field) use ($rows): bool {
    foreach ($rows as $row) {
        if (trim($row[$field['col']]) !== '') {
            return true;
        }
    }
    return false;
});

// Card grid built from the fields that are left: lap number and lap time on
// top, then one column per field and the time logged.
$gridCols  = max(count($fields), 1) + 1;
$gridAreas = '"' . str_repeat('lap ', $gridCols - 1) . 'laptime" '
           . '"' . ($fields ? implode(' ', array_keys($fields)) : '.') . ' when"';

$banner = '';
$bannerClass = 'ok';
if (isset($_GET['cleared'])) {
    if ($_GET['cleared'] === '1') {
        $banner = 'Log cleared.';
    } elseif ($_GET['cleared'] === 'key') {
        $banner      = 'Wrong key, the log was not cleared.';
        $bannerClass = 'err';
    } else {
        $banner      = 'Could not clear the log file — check that the data folder exists and is writable.';
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
        --warn: #ff9f0a;
        --err: #ff453a;
    }
    * { box-sizing: border-box; }
    body {
        /* A centred column, so a lap card is not stretched across a wide
           screen. */
        max-width: 688px;
        margin: 0 auto;
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
        justify-content: flex-end;
        margin-bottom: 14px;
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
    /* One card per lap in a single column, newest at the top, on every
       screen. Lap number and a large lap time on top, oil, coolant and the
       time logged below. The page itself scrolls, not an inner box. */
    table {
        display: block;
        width: 100%;
        font-variant-numeric: tabular-nums;
    }
    thead { display: none; }
    tbody {
        display: grid;
        grid-template-columns: 1fr;
        gap: 10px;
    }
    tbody tr {
        display: grid;
        /* Set on the table from the fields that have data, see $gridAreas. */
        grid-template-columns: repeat(var(--cols), 1fr);
        grid-template-areas: var(--areas);
        gap: 6px 12px;
        align-items: end;
        padding: 12px 14px;
        background: var(--panel);
        border: 1px solid var(--line);
        border-radius: 10px;
    }
    tbody tr:first-child { border-color: rgba(77, 163, 255, .55); }
    tbody tr:first-child td { color: var(--accent); font-weight: 600; }
    tbody td {
        font-size: .9rem;
        text-align: left;
    }
    td.c-lap { grid-area: lap; font-size: 1rem; }
    td.c-lap::before {
        content: "Lap ";
        color: var(--muted);
        font-weight: 400;
    }
    td.c-laptime {
        grid-area: laptime;
        text-align: right;
        font-size: 1.4rem;
        font-weight: 600;
    }
    td.c-oil     { grid-area: oil; }
    td.c-coolant { grid-area: coolant; }
    td.c-fuel    { grid-area: fuel; }
    td.c-when    { grid-area: when; text-align: right; font-size: .8rem; color: var(--muted); }
    td[data-label]::before {
        content: attr(data-label);
        display: block;
        font-size: .7rem;
        font-weight: 600;
        letter-spacing: .06em;
        text-transform: uppercase;
        color: var(--muted);
    }
    .empty {
        padding: 48px 16px;
        text-align: center;
        color: var(--muted);
        background: var(--panel);
        border: 1px solid var(--line);
        border-radius: 10px;
    }
    .empty code {
        background: #222834;
        padding: 2px 6px;
        border-radius: 4px;
        font-size: .85em;
    }

    /* Phones: a little less space around the page. */
    @media (max-width: 600px) {
        body { padding: 16px; }
        h1 { font-size: 1.15rem; }
        /* iOS zooms into inputs with text smaller than 16px. */
        .confirm input { font-size: 16px; }
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
    <table style="--cols: <?= (int) $gridCols ?>; --areas: <?= e($gridAreas) ?>;">
        <thead>
            <tr>
                <th>Lap</th>
                <th>Lap time</th>
<?php foreach ($fields as $field): ?>
                <th><?= e($field['head']) ?></th>
<?php endforeach; ?>
                <th>Time</th>
            </tr>
        </thead>
        <tbody>
<?php foreach ($displayRows as $index => $row): ?>
            <tr>
                <td class="c-lap"><?= fmt_cell($row[3]) ?><?php if (is_test_row($row)): ?><span class="tag">test</span><?php endif; ?></td>
                <td class="c-laptime"><?= trim($row[4]) === '' ? '—' : e(fmt_lap_time($row[4])) ?></td>
<?php foreach ($fields as $area => $field): ?>
                <td class="c-<?= $area ?>" data-label="<?= e($field['label']) ?>"><?= $field['fmt']($row[$field['col']]) ?></td>
<?php endforeach; ?>
                <td class="c-when" data-label="Time"><?= e(fmt_time($row[0])) ?></td>
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
