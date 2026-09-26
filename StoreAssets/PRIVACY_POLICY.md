# Bluetooth Audio Codec Privacy Policy

Effective date: September 15, 2026

Bluetooth Audio Codec performs its work locally on the user's Windows device.
It does not collect, retain, sell, or transmit personal information.

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

## Local diagnostic log

If an operation fails, the application can write technical exception details
to a size-limited diagnostic log in the user's local application-data folder.
The log is used for local troubleshooting, is never uploaded automatically, and
is rotated after it reaches 1 MB so that only the current and previous files are
kept. The user can delete these files at any time.

## Network access and third parties

The application does not use analytics, advertising, tracking, cloud storage,
or third-party data services. It does not transmit the detected codec or device
name over the network.

The application includes user-initiated links to Ko-fi and Afdian for supporting
development. Opening either link launches the user's default web browser. The
application does not send codec observations, device information, or other app
data to those services. Once the browser opens a third-party site, that site's
own privacy policy applies.

## Administrator access

Windows administrator approval is requested only after the user starts codec
detection. The elevated helper reads the restricted Bluetooth A2DP trace,
returns the result locally to the normal application process, and exits. It does
not change Bluetooth or system settings.

## Contact

e-mail: ben_li06@outlook.com
