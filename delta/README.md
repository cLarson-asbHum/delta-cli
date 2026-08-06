# Delta CLI

This tool is designed to compute patch files for the sake of minimizing file 
sizes, as well as reconstructing files from said patches. The patch files
(referred to as "deltas" or "delta files") use only string moves and symbol
additions.

This project directly implements the delta-computation algorithm described by
Tichy (cited below); reconstruction (sometimes internally called patching) is 
similarly derived from Tichy's work.


Citation for Tichy's work, as referenced above:
 > Tichy, Walter F., "The String-to-String Correction Problem with Block Moves"
 > (1983). Department of Computer Science Technical Reports. Paper 378.
 > https://docs.lib.purdue.edu/cstech/378

The code and plain text in this project is written solely by human authors and 
is licensed under the MIT Open Source License (see LICENSE for details).