# ixl_unlock

This utility is inteded to be used on FreeBSD with if_ixl(4) driver.
It disables Module Qualification check to be able use any SFPs.

NOTE: since the datacheet is not clear about the location of PHY
capability data structure, I made some assumptions when did the
search. If my assumptions are wrong, this can damage your card.
If you are not sure, do not use this utility.

At least try to use ixl_unlock with -g option and check that
all described values are correct.

Currently tested only with 0x15848086 card.
