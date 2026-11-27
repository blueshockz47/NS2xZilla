# RC1 release gates

Use disposable files and a backed-up SD card. Record console firmware, loader, client version, network, elapsed time, retries and failures. Do not mark untested cases passed.

- Launch and exit 20 times; start/stop FTP 20 times.
- Upload and download 10,000 test files; compare SHA-256 manifests.
- Delete 5,000 disposable files and empty folders; verify results.
- Interrupt uploads and downloads, resume, compare final hashes; test zero-byte files and invalid resume offsets.
- Disconnect and restore Wi-Fi during transfers; verify recovery and stop during recovery.
- Test one through four clients; verify fifth receives 421 and existing clients remain usable.
- Test eight-hour session and files above 4 GiB only where filesystem and available space support them.
- Test SwitchNest Windows, Linux, Android and a standard FTP client.
- Check browser layout, long paths, counters, uptime and diagnostics.

Release gate: no crashes, no silent corruption, no unrecovered failures in tested scenarios. Document any remaining limitations. CI compilation is not hardware validation.
