# logrotate

The logrotate utility is designed to simplify the administration of log files on a system which generates a lot of log files. Logrotate allows for the automatic rotation compression, removal and mailing of log files. Logrotate can be set to handle a log file daily, weekly, monthly or when the log file gets to a certain size.

## Download

The latest release is:

* [logrotate-3.11.0](https://github.com/logrotate/logrotate/releases/download/3.11.0/logrotate-3.11.0.tar.xz) ([sig](https://github.com/logrotate/logrotate/releases/download/3.11.0/logrotate-3.11.0.tar.xz.asc)) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.11.0))

Previous releases:

* [logrotate-3.10.0](https://github.com/logrotate/logrotate/releases/download/3.10.0/logrotate-3.10.0.tar.gz) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.10.0))
* [logrotate-3.9.2](https://github.com/logrotate/logrotate/releases/download/3.9.2/logrotate-3.9.2.tar.gz) ([Changelog](https://github.com/logrotate/logrotate/releases/tag/3.9.2))
* [logrotate-3.9.1](https://fedorahosted.org/releases/l/o/logrotate/logrotate-3.9.1.tar.gz)
* [logrotate-3.9.0](https://fedorahosted.org/releases/l/o/logrotate/logrotate-3.9.0.tar.gz)
* [logrotate-3.8.9](https://fedorahosted.org/releases/l/o/logrotate/logrotate-3.8.9.tar.gz)
* [logrotate-3.8.8](https://fedorahosted.org/releases/l/o/logrotate/logrotate-3.8.8.tar.gz)
* [logrotate-3.8.7](https://fedorahosted.org/releases/l/o/logrotate/logrotate-3.8.7.tar.gz)


## Unpack and verify

After downloading the tarball and .asc signature file, extract the tar file.

    $ tar -xJf logrotate-3.11.0.tar.xz
    

Get Kamil's PGP key rsa4096/72A37B36
(almost any keyserver will do if pgp.mit.edu is temporarily unavailable):

    $ gpg --keyserver pgp.mit.edu --recv-key 72A37B36
    

Check the key fingerprint (992A 96E0 7505 6E79 CD82  14F9 873D B375 72A3 7B36)

    $ gpg --fingerprint 72A37B36
    

and verify the PGP signature on the distribution tarball:

 
    $ gpg --verify logrotate-3.11.0.tar.xz.asc logrotate-3.11.0.tar.xz
    

If succesful your GPG output should look like this:

    gpg: Signature made Fri 02 Dec 2016 08:30:39 AM EST
    gpg:                using RSA key 873DB37572A37B36
    gpg: Good signature from "Kamil Dudka <kdudka@redhat.com>" [unknown]
    gpg: WARNING: This key is not certified with a trusted signature!
    gpg:          There is no indication that the signature belongs to the owner.
    Primary key fingerprint: 992A 96E0 7505 6E79 CD82  14F9 873D B375 72A3 7B36

# Patches and Questions

Open issues or pull requests on GitHub.
