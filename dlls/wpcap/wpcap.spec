@ stub bpf_dump
@ stub bpf_filter
@ stub bpf_image
@ stub bpf_validate
@ stub endservent
@ stub eproto_db
@ stub getservent
@ stub install_bpf_program
@ stub pcap_breakloop
@ stub pcap_close
@ cdecl pcap_compile(ptr ptr str long long) wine_pcap_compile
@ stub pcap_compile_nopcap
@ stub pcap_createsrcstr
@ cdecl pcap_datalink(ptr) wine_pcap_datalink
@ stub pcap_datalink_name_to_val
@ stub pcap_datalink_val_to_description
@ stub pcap_datalink_val_to_name
@ stub pcap_dispatch
@ stub pcap_dump
@ stub pcap_dump_close
@ stub pcap_dump_file
@ stub pcap_dump_flush
@ stub pcap_dump_ftell
@ stub pcap_dump_open
@ stub pcap_file
@ stub pcap_fileno
@ cdecl pcap_findalldevs(ptr str) wine_pcap_findalldevs
@ stub pcap_findalldevs_ex
@ cdecl pcap_freealldevs(ptr) wine_pcap_freealldevs
@ stub pcap_freecode
@ stub pcap_get_airpcap_handle
@ cdecl pcap_geterr(ptr) wine_pcap_geterr
@ stub pcap_getevent
@ stub pcap_getnonblock
@ stub pcap_is_swapped
@ cdecl pcap_lib_version() wine_pcap_lib_version
@ stub pcap_list_datalinks
@ stub pcap_live_dump
@ stub pcap_live_dump_ended
@ cdecl pcap_lookupdev(str) wine_pcap_lookupdev
@ cdecl pcap_lookupnet(str ptr ptr str) wine_pcap_lookupnet
@ stub pcap_loop
@ cdecl pcap_major_version(ptr) wine_pcap_major_version
@ cdecl pcap_minor_version(ptr) wine_pcap_minor_version
@ stub pcap_next
@ stub pcap_next_etherent
@ stub pcap_next_ex
@ stub pcap_offline_filter
@ stub pcap_offline_read
@ stub pcap_open
@ stub pcap_open_dead
@ cdecl pcap_open_live(str long long long str) wine_pcap_open_live
@ stub pcap_open_offline
@ stub pcap_parsesrcstr
@ stub pcap_perror
@ stub pcap_read
@ stub pcap_remoteact_accept
@ stub pcap_remoteact_cleanup
@ stub pcap_remoteact_close
@ stub pcap_remoteact_list
@ stub pcap_sendpacket
@ stub pcap_sendqueue_alloc
@ stub pcap_sendqueue_destroy
@ stub pcap_sendqueue_queue
@ stub pcap_sendqueue_transmit
@ stub pcap_set_datalink
@ cdecl pcap_setbuff(ptr long) wine_pcap_setbuff
@ cdecl pcap_setfilter(ptr ptr) wine_pcap_setfilter
@ stub pcap_setmintocopy
@ stub pcap_setmode
@ stub pcap_setnonblock
@ stub pcap_setsampling
@ stub pcap_setuserbuffer
@ stub pcap_snapshot
@ cdecl pcap_stats(ptr ptr) wine_pcap_stats
@ stub pcap_stats_ex
@ stub pcap_strerror
@ stub wsockinit
