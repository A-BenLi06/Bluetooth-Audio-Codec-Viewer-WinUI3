# Bluetooth Audio Codec Privacy Policy

Effective date: August 9, 2026

Bluetooth Audio Codec performs its work locally on the user's Windows device.
It does not collect, retain, sell, or transmit personal information.

## Data processed locally

When the user starts a detection, the application reads the friendly name of
the default Windows audio output and Bluetooth A2DP codec identifiers emitted by
the local Windows ETW provider. These values are displayed in the application
and are not written to a remote service.

The application does not record audio content. The short diagnostic tone is
generated locally only to ask Windows to create a fresh A2DP stream.

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
