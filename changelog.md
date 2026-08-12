# delta-cli Changelog

## 1.0.0

This is the first version with full support for the essential commands. 
The main features are listed below:

  - Intuitive delta computation and reconstruction command-line syntax, 

  - Complete error handling,
  
  - Options for logging, such as `--quiet` or `--verbose`, which allow a   
    user to tweak their CLI experience,
    
  - Automatic and manual output destination specification, with collision 
    options including `--prompt`,

  - Fault-tolerant delta file deserialization.

A known issue with this version is the lack of file hashing. No hashes are 
generated for computed delta files, and no hashes are verified during 
reconstruction. This is expected to be fixed by 1.1.0.