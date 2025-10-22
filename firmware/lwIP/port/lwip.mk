#===============================================================================
# lwIP Build Configuration for PicoRV32
# Include this in firmware/Makefile when USE_LWIP=1
#===============================================================================

# lwIP directories (relative to firmware/ directory)
LWIP_DIR = ../downloads/lwip
LWIP_PORT_DIR = lwIP/port

# lwIP include paths
LWIP_INCLUDES = \
	-I$(LWIP_DIR)/src/include \
	-I$(LWIP_PORT_DIR) \
	-I$(LWIP_PORT_DIR)/arch

# lwIP source files (NO_SYS mode - bare metal)
LWIP_CORE_SRCS = \
	$(LWIP_DIR)/src/core/init.c \
	$(LWIP_DIR)/src/core/def.c \
	$(LWIP_DIR)/src/core/dns.c \
	$(LWIP_DIR)/src/core/inet_chksum.c \
	$(LWIP_DIR)/src/core/ip.c \
	$(LWIP_DIR)/src/core/mem.c \
	$(LWIP_DIR)/src/core/memp.c \
	$(LWIP_DIR)/src/core/netif.c \
	$(LWIP_DIR)/src/core/pbuf.c \
	$(LWIP_DIR)/src/core/raw.c \
	$(LWIP_DIR)/src/core/stats.c \
	$(LWIP_DIR)/src/core/sys.c \
	$(LWIP_DIR)/src/core/tcp.c \
	$(LWIP_DIR)/src/core/tcp_in.c \
	$(LWIP_DIR)/src/core/tcp_out.c \
	$(LWIP_DIR)/src/core/timeouts.c \
	$(LWIP_DIR)/src/core/udp.c

LWIP_IPV4_SRCS = \
	$(LWIP_DIR)/src/core/ipv4/autoip.c \
	$(LWIP_DIR)/src/core/ipv4/dhcp.c \
	$(LWIP_DIR)/src/core/ipv4/etharp.c \
	$(LWIP_DIR)/src/core/ipv4/icmp.c \
	$(LWIP_DIR)/src/core/ipv4/igmp.c \
	$(LWIP_DIR)/src/core/ipv4/ip4_frag.c \
	$(LWIP_DIR)/src/core/ipv4/ip4.c \
	$(LWIP_DIR)/src/core/ipv4/ip4_addr.c

LWIP_NETIF_SRCS = \
	$(LWIP_DIR)/src/netif/slipif.c

LWIP_PORT_SRCS = \
	$(LWIP_PORT_DIR)/sio.c \
	$(LWIP_PORT_DIR)/sys_arch.c

LWIP_APPS_SRCS = \
	$(LWIP_DIR)/src/apps/lwiperf/lwiperf.c

# All lwIP sources
LWIP_SRCS = $(LWIP_CORE_SRCS) $(LWIP_IPV4_SRCS) $(LWIP_NETIF_SRCS) $(LWIP_PORT_SRCS) $(LWIP_APPS_SRCS)

# Object files
LWIP_OBJS = $(LWIP_SRCS:.c=.o)

# Compiler flags for lwIP
LWIP_CFLAGS = $(LWIP_INCLUDES)

#===============================================================================
# Build Rules
#===============================================================================

# Pattern rule for lwIP source compilation
$(LWIP_DIR)/%.o: $(LWIP_DIR)/%.c
	@echo "  CC (lwIP)  $<"
	@$(CC) $(CFLAGS) $(LWIP_CFLAGS) -c $< -o $@

$(LWIP_PORT_DIR)/%.o: $(LWIP_PORT_DIR)/%.c
	@echo "  CC (port)  $<"
	@$(CC) $(CFLAGS) $(LWIP_CFLAGS) -c $< -o $@

# Clean lwIP objects
.PHONY: clean-lwip
clean-lwip:
	@echo "Cleaning lwIP objects..."
	@rm -f $(LWIP_OBJS)
