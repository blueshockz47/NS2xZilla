# SwitchNest interoperability

OunceShell Pro identifies itself with the standard FTP greeting `220 OunceShell Pro FTP ready`. Clients may detect this string for display only; it is not authentication. Continue to accept other valid 220 greetings.

Use FEAT to discover capabilities, PASV for passive connections, and binary TYPE I for files. REST STREAM is advertised; issue REST immediately before RETR or STOR and require a 350 reply. For uploads, verify remote SIZE before resuming; if the remote file is larger than the intended file, restart from zero. Verify final size and preferably an independently computed hash. Reconnect and restore the working directory after a broken connection. Do not retry destructive operations blindly: check the resulting directory state first.

The server limits simultaneous control sessions to four. Handle 421 with bounded backoff. Treat 426 as interrupted transfer, 451 as local processing failure, and 550 as an operation failure requiring inspection. FTP is unencrypted and this greeting does not establish trust; use only a trusted isolated network.

## Hardware acceptance matrix

Run independently with SwitchNest Windows, Linux and Android: connect, browse, upload, download, resume both directions after interruption, compare hashes, create folders, delete files and empty folders, reconnect after network loss, and stop the server. Record versions, retry counts, errors and diagnostic logs. These are required tests, not claimed results.
