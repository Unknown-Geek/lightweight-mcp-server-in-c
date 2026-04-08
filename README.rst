===========================
Lightweight MCP Server in C
===========================

Module 1 & 2 – Basic MCP Server Integration
==========================================

Files Added
-----------

* ``vswitchd/mcp_server.c``  
* ``vswitchd/mcp_server.h``  

Files Modified
--------------

* ``vswitchd/ovs-vswitchd.c``
    * Called ``mcp_server_init()`` during startup.
    * Called ``mcp_server_run()`` inside the main loop.
    * Called ``mcp_server_close()`` during shutdown.

* ``vswitchd/automake.mk``
    * Added ``mcp_server.c`` so it gets compiled.

Functionality
-------------

* **Server Port:** ``8080``  
* **Endpoint:** ``POST /mcp``  
* **Response:** ``{"status": "ok"}``

Setup and Run
=============

Build OVS
---------

.. code-block:: bash

    ./boot.sh
    ./configure
    make -j4
    sudo make install

Start OVS
---------

Start database:

.. code-block:: bash

    sudo ovsdb-server \
    --remote=punix:/usr/local/var/run/openvswitch/db.sock \
    --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
    --pidfile --detach

Initialize DB:

.. code-block:: bash

    sudo ovs-vsctl --no-wait init

Start switch :

.. code-block:: bash

    sudo ovs-vswitchd --pidfile --detach

Test Endpoint
-------------

.. code-block:: bash

    curl -X POST http://localhost:8080/mcp
