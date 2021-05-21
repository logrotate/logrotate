# logrotate

The logrotate utility is designed to simplify the administration of log files on a system which generates a lot of log files. Logrotate allows for the automatic rotation compression, removal and mailing of log files. Logrotate can be set to handle a log file hourly, daily, weekly, monthly or when the log file gets to a certain size.

## Download

The latest release is:

* [logrotate-3.18.1](https://github.com/logrotate/logrotate/releases/download/3.18.1/logrotate-3.18.1.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.18.1/logrotate-3.18.1.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.18.1))

Previous releases:

* [logrotate-3.18.0](https://github.com/logrotate/logrotate/releases/download/3.18.0/logrotate-3.18.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.18.0/logrotate-3.18.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.18.0))
* [logrotate-3.17.0](https://github.com/logrotate/logrotate/releases/download/3.17.0/logrotate-3.17.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.17.0/logrotate-3.17.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.17.0))
* [logrotate-3.16.0](https://github.com/logrotate/logrotate/releases/download/3.16.0/logrotate-3.16.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.16.0/logrotate-3.16.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.16.0))
* [logrotate-3.15.1](https://github.com/logrotate/logrotate/releases/download/3.15.1/logrotate-3.15.1.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.15.1/logrotate-3.15.1.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.15.1))
* [logrotate-3.15.0](https://github.com/logrotate/logrotate/releases/download/3.15.0/logrotate-3.15.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.15.0/logrotate-3.15.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.15.0))
* [logrotate-3.14.0](https://github.com/logrotate/logrotate/releases/download/3.14.0/logrotate-3.14.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.14.0/logrotate-3.14.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.14.0))
* [logrotate-3.13.0](https://github.com/logrotate/logrotate/releases/download/3.13.0/logrotate-3.13.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.13.0/logrotate-3.13.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.13.0))
* [logrotate-3.12.3](https://github.com/logrotate/logrotate/releases/download/3.12.3/logrotate-3.12.3.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.12.3/logrotate-3.12.3.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.12.3))
* [logrotate-3.12.2](https://github.com/logrotate/logrotate/releases/download/3.12.2/logrotate-3.12.2.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.12.2/logrotate-3.12.2.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.12.2))
* [logrotate-3.12.1](https://github.com/logrotate/logrotate/releases/download/3.12.1/logrotate-3.12.1.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.12.1/logrotate-3.12.1.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.12.1))
* [logrotate-3.12.0](https://github.com/logrotate/logrotate/releases/download/3.12.0/logrotate-3.12.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.12.0/logrotate-3.12.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.12.0))
* [logrotate-3.11.0](https://github.com/logrotate/logrotate/releases/download/3.11.0/logrotate-3.11.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.11.0/logrotate-3.11.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.11.0))
* [logrotate-3.10.0](https://github.com/logrotate/logrotate/releases/download/3.10.0/logrotate-3.10.0.tar.gz) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.10.0))
* [logrotate-3.9.2](https://github.com/logrotate/logrotate/releases/download/3.9.2/logrotate-3.9.2.tar.gz) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.9.2))

## Git checkout

You can also obtain code by using git checkout:
```
git clone https://github.com/logrotate/logrotate.git -b master
```

Replace `master` with branch or tag you intend to checkout

## Verify and unpack

After downloading the tarball and .asc signature file, check the signature:

Get Kamil's PGP key rsa4096/72A37B36
(almost any keyserver will do if pgp.mit.edu is temporarily unavailable):

    $ gpg --keyserver pgp.mit.edu --recv-key 992A96E075056E79CD8214F9873DB37572A37B36

and verify the PGP signature on the distribution tarball:


    $ gpg --verify logrotate-3.11.0.tar.xz.asc logrotate-3.11.0.tar.xz


If successful your GPG output should look like this:

    gpg: Signature made Fri 02 Dec 2016 08:30:39 AM EST
    gpg:                using RSA key 873DB37572A37B36
    gpg: Good signature from "Kamil Dudka <kdudka@redhat.com>" [unknown]
    gpg: WARNING: This key is not certified with a trusted signature!
    gpg:          There is no indication that the signature belongs to the owner.
    Primary key fingerprint: 992A 96E0 7505 6E79 CD82  14F9 873D B375 72A3 7B36

You may then unpack the tarball:

    $ tar -xJf logrotate-3.11.0.tar.xz

Notice that git tags are signed with same key:

    $ git tag --verify 3.11.0

## Compiling

Obtain source either by [Downloading](#download) it or doing [Git checkout](#git-checkout).

Install dependencies for Debian systems:
```
apt-get update
apt-get install autoconf automake libpopt-dev libtool make xz-utils
```

Install dependencies for Fedora/CentOS systems:

```
yum install autoconf automake libtool make popt-devel xz
```

Compilation (`autoreconf` is optional if you obtained source from tarball):
```
cd logrotate-X.Y.Z
autoreconf -fiv
./configure
make
```

# Patches and Questions

Open issues or pull requests on GitHub.
