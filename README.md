# uTorrent IRC Chat Bot Remote controller (in C)

This is an IRC Chat Bot remote controller for uTorrent. Detailed description is available on the [blog post](https://biniamdemissie.com/2013/08/05/an-irc-bot-remote-controller-with-c/).

In order to use it you need to:
* have uTorrent running a web service,
* choose an IRC server + port
* IRC channel
* nick [password]

Surrported commands (# represents the index of the torrent):
* !list - no argv
* !stop #
* !pause #
* !recheck #
* !force #
* !add url (direct link)
* !update
* !supdate

Note that every command starts with exclamation mark (!). Example command looks like the following:
<pre>
!list
[1] Some.File                                       700MB   340KB/s  68%
[2] Some.Other.File                                 110MB   240KB/s  24%
</pre>
