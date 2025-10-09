### Redirect with UDP and TCP

```plantuml

component "server" as server {
    component "udp port" as sudp1 {

    }

    component "udp port" as sudp2 {

    }

    component "tcp port" as tcp {

    }

    component "server app" as server_app {
        component "udp port" as saudp {

        }

    }
}

component "client1" as client1 {

    component "client app" as client_app1 {
        component "udp port" as caudp1 {

        }

    }

    component "udp port" as udp1 {

    }

    component "tcp port" as tcp1 {

    }

}


component "client2" as client2 {

    component "tcp port" as tcp2 {

    }

    component "udp port" as udp2 {

    }

    component "client app" as client_app2 {
        component "udp port" as caudp2 {

        }

    }
}


tcp1 <-down-> tcp : "data 1"
tcp2 <-down-> tcp : "data 2"

sudp1  <-up-> tcp : "data 1"
sudp2  <-up-> tcp : "data 2"

sudp1 <-down-> saudp : "data 1"
sudp2 <-down-> saudp : "data 2"

tcp2 <--> udp2: "data 2"
caudp2 <--> udp2: "data 2"


tcp1 <--> udp1: "data 1"
caudp1 <--> udp1: "data 1"

```


### Redirect with UDP and Virtual Channel

```plantuml

component "server" as server {
    component "udp port" as sudp1 {

    }

    component "udp port" as sudp2 {

    }

    component "Virtual Channel 1" as svc1 {

    }

    component "Virtual Channel 2" as svc2 {

    }

    component "server app" as server_app {
        component "udp port" as saudp {

        }

    }
}

component "client1" as client1 {

    component "client app" as client_app1 {
        component "udp port" as caudp1 {

        }

    }

    component "udp port" as udp1 {

    }

    component "virtual channel 1" as cvc1 {

    }

}


component "client2" as client2 {

    component "Virtual Channel 2" as cvc2 {

    }

    component "udp port" as udp2 {

    }

    component "client app" as client_app2 {
        component "udp port" as caudp2 {

        }

    }
}


cvc1 <-down-> svc1 : "data 1"
cvc2 <-down-> svc2 : "data 2"

sudp1  <-up-> svc1 : "data 1"
sudp2  <-up-> svc2 : "data 2"

sudp1 <-down-> saudp : "data 1"
sudp2 <-down-> saudp : "data 2"

cvc2 <--> udp2: "data 2"
caudp2 <--> udp2: "data 2"


cvc1 <--> udp1: "data 1"
caudp1 <--> udp1: "data 1"


```

The virtual channel can be implemented with different protocols. In this version, we will use multiple tcp connections to implement the virtual channel. Each virtual channel will have its own tcp connections. It will make sure the data has the same order as it was sent.
