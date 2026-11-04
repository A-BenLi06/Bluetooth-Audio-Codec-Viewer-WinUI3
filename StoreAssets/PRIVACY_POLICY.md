# Bluetooth Audio Codec Privacy Policy

Effective date: October 27, 2026

Bluetooth Audio Codec processes Bluetooth and audio state locally on the user's
Windows device. It keeps bounded local diagnostic information to help explain
errors. It does not sell information or automatically upload reports.

## Data processed locally

When the user starts a detection, the application reads the friendly name of
the default Windows audio output and Bluetooth A2DP codec identifiers emitted by
the local Windows ETW provider. These values are displayed in the application
and are not written to a remote service.

While the application window is open, it also watches the local connection
state and display name of paired Bluetooth audio devices so the interface can
refresh when a headset connects or disconnects. It does not initiate pairing,
connect to a device, or change device settings.

The application does not record audio content. The short diagnostic tone is
generated locally only to ask Windows to create a fresh A2DP stream.

## Local diagnostic reports and technical log

Each detection, including a blocked preflight, retains its build/system
versions, structured phases and result, numeric errors, available helper/ETW
statistics, and minimal target device/container/topology relationships. Device
relationships use local tags rather than the original addresses, PnP IDs or
container GUIDs. Structured detection files do not copy friendly device nicknames,
raw ETL, process dumps, audio, account details or authentication tokens.

Detection snapshots also keep available Windows product model and manufacturer
properties for connected Classic Bluetooth Audio/Video devices and the resolved
default Bluetooth output. Each property identifies its Windows source and is
marked as not user-confirmed. Missing properties remain unknown; the app does
not infer a product model from a device nickname, address or driver provider.
PnP manufacturer values may identify a generic software vendor rather than the
headset brand. The list is bounded to eight devices and explicitly reports
omissions or unavailable reads; it is not a complete inventory of LE devices.
These cached property reads do not initiate a scan or a device connection.

The local report store retains up to five failed/interrupted detections plus
the latest success/cancellation, for at most seven days and within a 10 MiB
disk budget. Cleanup happens when the report store is accessed. The user can
choose an attempt or application/startup context, inspect each report file and save a ZIP. Exported
files are kept at the location the user chooses and are not removed by the
internal retention policy.

The application writes bounded technical logs for startup, connection changes,
detection stages, diagnostic playback, ETW event/schema statistics, and errors
in the user's local application-data folder. Reports also include application,
Windows, and Bluetooth driver versions. Audio content and full ETW payloads are
not recorded. The technical log uses salted per-process
digests and filters common paths, addresses, identifiers and token patterns.
Pattern filtering is not a guarantee that arbitrary text is anonymous; review
text logs manually before sharing them.

The single Report a problem flow can optionally include a filtered recent
runtime log as runtime-log.txt in the same ZIP. It is collected while preparing
the report, can cover several detections, and includes environment/output state
read at export time. This is labeled separately from saved detection snapshots.
The supplement is limited to 512 KiB; truncation, collection time, and read
failure are disclosed in the manifest. It is off by default for a selected
detection and initially selected for application/startup reports, where it can
be unchecked. Such reports do not claim that a detection or ETW capture ran.

Logs rotate at approximately 1 MiB, retaining only the current and previous
files. The user can delete them at any time or include them through Report a
problem and inspect the combined ZIP before sharing. A legacy TXT command-line
export remains available for compatibility; there is no separate TXT button.
Nothing is uploaded automatically.

## Network access and third parties

The application does not contain an analytics, advertising, tracking or cloud
upload service. It does not automatically send codec observations or device
names over the network.

After saving a standard report, the user may choose to open GitHub in their
default browser. The URL pre-fills the report ID, build ID, stable reason and
an editable issue prompt. The application does not send the report file,
browser credentials or cookies. Copying a summary writes it to the Windows
clipboard and is also initiated by the user.

GitHub sign-in and file submission happen in the browser under GitHub's own
policies. Issues and attachments in this project's public repository are
public; public attachments can be accessed without signing in. Selecting a
file uploads it immediately, before the final issue submission. The app
explains this before opening GitHub. Opening the page is not confirmation that
an issue was submitted or a maintainer received it. Do not put raw ETL, dumps
or unfiltered device records in public Issues. Sharing any restricted data
requires a separately agreed private destination and access scope.

Windows may provide Microsoft with crash/hang diagnostics according to the
user's Windows settings and Microsoft's policies. This is separate from the
application's local reports. The app does not change those settings or promise
that every failure will appear in Store Health.

The application includes user-initiated links to Ko-fi and Afdian for supporting
development. Opening either link launches the user's default web browser. The
application does not send codec observations, device information, or other app
data to those services. Once the browser opens a third-party site, that site's
own privacy policy applies.

## Administrator access

When you select Detect, the ordinary application process attempts to pause all
currently playing sessions exposed through Windows system media controls,
including players using other output devices. After detection or cancellation it
attempts to resume only sessions it actually paused. Initially paused sessions
are not started. Other audio sources may need manual attention. It does not
terminate players, mute devices or disconnect Bluetooth.

A separate target-audio observation supplies process basenames for local hints;
those names and paths are not exported. The report records status and numeric
errors with attempt-local session numbers, without application IDs, process IDs,
media titles or playback URLs. To avoid resuming a changed item, the app compares
title, artist, album and track metadata in memory with a random per-attempt key.
Metadata, comparison digests and keys are not saved/exported; the key is cleared
when playback cleanup is released. No window titles or page URLs are read.

Windows administrator approval is requested only after the user starts codec
detection. The elevated helper reads the restricted Bluetooth A2DP trace,
returns the result locally to the normal application process, and exits. It does
not change Bluetooth or system settings.

## Contact

e-mail: ben_li06@outlook.com
