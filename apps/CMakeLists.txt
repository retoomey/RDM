# Toomey May 2026
# Applications using the rdm library
#

# ==============================================================================
# The main server
# ==============================================================================
# Executable Definition (rdmd)
# Executable Definition (rdmbroker) -- per socket connection for rdmd
# This is different than original ldm.  Original ldmd forked itself.
# Now it spawns rpqbroker for each of the incoming connection sockets. 
# This makes rdmd a true supervisor.
# ==============================================================================
addLDMBinary(rdmd
    SOURCES ldmd/ldmd.cpp
)
addLDMBinary(rdmbroker
    SOURCES pqbroker.cpp
)

# ==============================================================================
# Local Product Queue tools
# ==============================================================================

# ==============================================================================
# lpqcreate -- Create a new queue
addLDMBinary(lpqcreate
    SOURCES pqcreate.cpp
)

# ==============================================================================
# lpqinsert -- Insert/append into local queue
addLDMBinary(lpqinsert
    SOURCES pqinsert.cpp
)

# ==============================================================================
# lpqcheck -- Check a local queue for damage
addLDMBinary(lpqcheck   
    SOURCES pqcheck.cpp
)

# ==============================================================================
# lpqsend -- Monitors a local queue, pushing to a remote
addLDMBinary(lpqsend
    SOURCES pqsend.cpp
)

# ==============================================================================
# lpqmon -- Monitors a local queue
addLDMBinary(lpqmon    
    SOURCES pqmon.cpp
)

# ==============================================================================
# Server tools (typically these ignore local queue files and work with servers)
# ==============================================================================

# ==============================================================================
# rdmping -- Ping an ldm/rdm server
addLDMBinary(rdmping   
    SOURCES ldmping.cpp
)

# ==============================================================================
# rdmcat -- Pulls metadata or data from a server
#        -- Combines old feedme and notifyme
#        -- Server equivalent to lpqcat
addLDMBinary(rdmcat 
    SOURCES rdmpull.cpp
)

# ==============================================================================
# rdmsend -- Push a local file directly to a server.  Connects than disconnects
# In practice it's better to use lpqinsert to a local queue and lpqsend,
# less overhead  Especially with the modern posix flags
# ==============================================================================
addLDMBinary(rdmsend  
    SOURCES ldmsend.cpp
)

# ==============================================================================
# lpqutil -- local product queue utility
# ==============================================================================
addLDMBinary(lpqutil   
    SOURCES pqutil.cpp
)

# ==============================================================================
# lpqcat -- cat a local queue.  The local version of rdmcat
# ==============================================================================
addLDMBinary(lpqcat
    SOURCES pqcat.cpp
)

# ==============================================================================
# lpqact -- The downstream action/file-dispatching engine 
addLDMBinary(lpqact
    SOURCES 
        pqact/pqact.cpp
        pqact/ActionFactory.cpp
        pqact/FileCache.cpp
        pqact/FileOpsUtil.cpp
        pqact/UnioEntry.cpp
        pqact/PipeEntry.cpp
        pqact/FileAction.cpp
        pqact/PipeAction.cpp
        pqact/ExecAction.cpp
        pqact/PipeBuffer.cpp
        pqact/StateFile.cpp
        pqact/PqactConfig.cpp
        pqact/PqactParser.cpp
)

# ==============================================================================
# Misc Tools for interacting with database or registry
# FIXME: Haven't tested these much yet manually, only with test scripts

# ==============================================================================
# Executable Definition (ruldbutil)
# ==============================================================================
addLDMBinary(ruldbutil    
    SOURCES uldbutil.cpp
)

# ==============================================================================
# Executable Definition (rregutil)
# ==============================================================================
addLDMBinary(rregutil
    SOURCES regutil.cpp
)
