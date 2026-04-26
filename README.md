GTK Project written in pure C features:
    . GTK GUI Interface
    . Drop down menu
    . Split panes
    . Resizeable Panes
    . Left Pane Terminal Emulator
    . Right pane API Interface to either OpenAI or Gemini
    . "TEE" mode that pipes the termial date into the AI pane
    . It is going to need an API Key for either OpenAI or for Gemini
  This is a work in progress, cyrrently version 0.4. Its in its infancy stage 
  but has potetial in being a very useful tool once all the features are implimented 

  Build Deps
  CentOS:
sudo dnf update
sudo dnf groupinstall "Development Tools"
# Enable the CRB repository (required for json-c-devel)
sudo dnf config-manager --set-enabled crb
sudo dnf install pkgconf-pkg-config \
    gtk3-devel \
    vte291-devel \
    libcurl-devel \
    json-c-devel \
    mariadb-devel

OpenSUSE Tumbleweed:
sudo zypper refresh
sudo zypper install gcc make pkg-config \
    gtk3-devel \
    vte-devel \
    libcurl-devel \
    libjson-c-devel \
    libmariadb-devel

Ubuntu:
sudo apt update
sudo apt install build-essential pkg-config \
    libgtk-3-dev \
    libvte-2.91-dev \
    libcurl4-openssl-dev \
    libjson-c-dev \
    libmariadb-dev

After dependancies are satisfied all that needs to be done is make, then the binary aiterm just copy it into your /usr/bin directory and the aiterm.conf needs to be placed in /etc. All needed configurations are in the aiterm.conf.example

let me know what you think or any suggestions please feel free.
pet.350.pt@gmail.com
Peter Talbott
