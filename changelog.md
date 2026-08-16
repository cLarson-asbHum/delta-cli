# delta-cli Changelog

## 1.1.0

This is a feature addition patch for on top of 1.0.0. 

### Breaking changes
  - None

### Additions & Improvements
  - SHA-256 is fully implemented and tested; by default the `delta` and 
    `reconstruct` commands will respectively generate and verify hashes,
    throwing errors if the hashes do not match. 
    
      * This is **NOT** a breaking change because (1) files from 1.0.0 will 
        simply have their hashes ignored, displaying a warning; and (2) old hash 
        generation can be reenabled by using the `--ignore-hash` flag

  - More log levels were added which allow a user to tweak how much logging is 
    done: `--warning,` `--reduced,` and `--detail.` Warning only permits warnings, 
    errors and prompts; reduced eliminates info messages, and detail adds some 
    messages (but fewer than --verbose)

  - Warnings can be treated as errors with the `--strict` flag. This will cause 
    the program to safely exit whenever a warning is caught, rather than 
    continuing as is the default.

### Bug Fixes
  - Argument slurp errors now print the correct message rather than a garbage 
    state message.

## 1.0.0

This is the first version with full support for the essential commands. 
The main features are listed below:

  - Intuitive delta computation and reconstruction command-line syntax, 

  - Complete error handling,
  
  - Options for logging, such as ``--quiet`` or ``--verbose`,` which allow a   
    user to tweak their CLI experience,
    
  - Automatic and manual output destination specification, with collision 
    options including `--prompt`,

  - Fault-tolerant delta file deserialization.

A known issue with this version is the lack of file hashing. No hashes are 
generated for computed delta files, and no hashes are verified during 
reconstruction. This is expected to be fixed by 1.1.0.