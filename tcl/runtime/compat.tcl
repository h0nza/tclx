#
# compat --
#
# This file provides commands compatible with older versions of Extended Tcl.
# 
#------------------------------------------------------------------------------
# Copyright 1992-1995 Karl Lehenbauer and Mark Diekhans.
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose and without fee is hereby granted, provided
# that the above copyright notice appear in all copies.  Karl Lehenbauer and
# Mark Diekhans make no representations about the suitability of this
# software for any purpose.  It is provided "as is" without express or
# implied warranty.
#------------------------------------------------------------------------------
# $Id: compat.tcl,v 4.0 1994/07/16 05:29:27 markd Rel markd $
#------------------------------------------------------------------------------
#

#@package: TclX-Compatibility execvp assign_fields

proc execvp {progname args} {
    error "The execvp command is outdated, use the execl command directly"
}
proc assign_fields {list args} {
    if [lempty $args] {
        return
    }
    return [uplevel lassign [list $list] $args]
}
