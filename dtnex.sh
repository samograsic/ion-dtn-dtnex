#/bin/bash
# DTNEX scrip
# Network Information Exchange Mechanism for DTN
# Author: Samo Grasic, samo@grasic.net

updateInterval=60 #Update time in seconds




echo "Starting a DTNEX script, author: Samo Grasic (samo@grasic.net), v0.3 ..."

serviceNr=12160 #Do not change
msgidentifier="xmsg"

capturePipe=receivedmsgpipe

rm $capturePipe
touch $capturePipe
chmod 644 $capturePipe


ionOutput=$(echo "v"|bpadmin)
IFS=' ' read -ra versionline <<< $ionOutput
echo "Using ION Version:$(tput setaf 3)${versionline[1]}$(tput setaf 7)"

#Getting locally registered  endpoints
bpadminOutput=$(echo "l endpoint"|bpadmin)

echo "*----------------------------------------------------------------------*"
#echo "Raw List of ENDPoints:"
#echo $bpadminOutput
#echo ""
#echo "Number of received lines:${#bpadminOutput[@]}"

localEIDs=($(echo "l endpoint"|bpadmin|grep -E -o "\bipn+:[0-9.-]+\.[0-9.-]+\b"))

echo "Number of registered  EIDs:${#localEIDs[@]}"
echo "List of local EIDs:"

for i in "${localEIDs[@]}"; do
  echo ">$i"
done


echo "Extracting the node EID number from the first registered  endpoint... ${localEIDs[0]}"
IFS=':' read -ra ipn_array <<< ${localEIDs[0]}
echo "ipnarray:${ipn_array[1]}"
IFS='.' read -ra nodeIdArray <<< ${ipn_array[1]}
nodeId=${nodeIdArray[0]};
echo "$(tput setaf 3)Parsed Node ID:$nodeId$(tput setaf 7)"




#Registering needed endpoints needed to exchange messages, do not change service nrs.
bpadminOutput1=$(echo "a endpoint ipn:$nodeId.$serviceNr q"|bpadmin)
#bpadminOutput2=$(echo "a endpoint ipn:$nodeId.$outgoingSerciceNr q"|bpadmin)

#Getting locally registered  endpoints
bpadminOutput=$(echo "l endpoint"|bpadmin)


bpsinkCommand="bpsink ipn:$nodeId.$serviceNr>$capturePipe&"
echo "Starting bpsink with:$bpsinkCommand"
bpsink ipn:$nodeId.$serviceNr>$capturePipe&
pid=$!


# If this script is killed, kill the child process.
trap "kill $pid 2> /dev/null" EXIT


# While bpsink is running...
while kill -0 $pid 2> /dev/null; do
    	# Do stuff
	echo
    	echo "Main loop..."
	echo "*----------------------------------------------------------------------*"
	#echo "Getting a plan list (neighbour  nodes)..."
	plans=($(echo "l plan"|ipnadmin|sed 's@^[^0-9]*\([0-9]\+\).*@\1@'))
	unset plans[-1] # removes the last 1 elements
	unset plans[-1] # removes the last 1 elements
	unset plans[-1] # removes the last 1 elements
	#echo "Number of configured plans:${#plans[@]}"
	echo "$(tput setaf 5)List of configured plans:"
	for i in "${plans[@]}"; do
  	echo ">$i"
	done


#        echo "Exchanging messages with configured plans:"

	for i in "${plans[@]}"; do
	plan=${i:0}
	if [[ "$nodeId" == "$plan" ]]; then
		echo "Skipping local loopback plan"
	else
  		echo "$(tput setaf 3)Messaging own plan to node [Origin:$nodeId, From:$nodeId, To:$plan, About:$nodeId]$(tput setaf 7)"
		bpsourceCommand="bpsource ipn:$plan.$serviceNr \"$msgidentifier 1 li $nodeId $nodeId $nodeId $plan\""
		#echo $bpsourceCommand
		eval $bpsourceCommand
	fi
	done

	#Processing received network messages


	cat $capturePipe|while read -r line
	do
  	#echo "$(tput setaf 5)Received line:$line"
	if grep -q $msgidentifier <<<$line; then
	    	#Routing message received, processing received command
		#echo "$(tput setaf 5)Routing message received, processing..."
		cmdarray=($line)
		#echo "Command array: ${cmdarray[@]}"
		#echo "Number of elements in the array: ${#cmdarray[@]}"
		if [[ "${cmdarray[1]}" == "1" ]];then
			#echo "Version 1 command detected, parsing command..."
			if [[ "${cmdarray[2]}" == "li" ]];then
				msgOrigin=${cmdarray[3]}
				msgSentFrom=${cmdarray[4]}
				nodeA=${cmdarray[5]}
				nodeB=${cmdarray[6]}
				echo "$(tput setaf 2)Link message received[Origin:$msgOrigin,From:$msgSentFrom,NodeA:$nodeA,NodeB:$nodeB], updation ION...$(tput setaf 7)"
                		ionadminCommandContact1="echo \"a contact +1 +3600000 $nodeA $nodeB 100000\"|ionadmin"
                		#echo $ionadminCommandContact1
				eval  $ionadminCommandContact1 >/dev/null
				ionadminCommandContact2="echo \"a contact +1 +3600000 $nodeB $nodeA 100000\"|ionadmin"
                                #echo $ionadminCommandContact2
                                eval $ionadminCommandContact2>/dev/null
				ionadminCommandRange1="echo \"a range +1 +3600000 $nodeA $nodeB 1\"|ionadmin"
                                #echo $ionadminCommandRange1
                                eval $ionadminCommandRange1>/dev/null
                                ionadminCommandRange2="echo \"a range +1 +3600000 $nodeB $nodeA 1\"|ionadmin"
                                #echo $ionadminCommandRange2
                                eval $ionadminCommandRange2>/dev/null
				#echo "We forward information to all neigboors, except to the node that send or create link message..."
			        for out in "${plans[@]}"; do
        				outd=${out:0}
        				if [ "$msgOrigin" == "$outd" ] || [ "$msgSentFrom" == "$outd" ]  || [ "$nodeId" == "$outd" ] ; then
                				#echo "Skipping sending message to ourself or to the msg source node:$msgOrigin that sent us this message..."
        					a=0
					else
                				echo "$(tput setaf 5)Forwarding message[Origin:$msgOrigin,From:$msgSentFrom,To:$outd,NodeA:$nodeA,NodeB:$nodeB]$(tput setaf 7)"
                				bpsourceForwardCommand="bpsource ipn:$out.$serviceNr \"$msgidentifier 1 li $msgOrigin $nodeId $nodeA $nodeB\""
                				#echo $bpsourceForwardCommand
                				eval $bpsourceForwardCommand>/dev/null
        				fi
        			done

			else
                        	echo "Unknown command received!"

			fi
		else
			echo "Unknown version command received!"
		fi
	#else
	#	echo "Unknown message received!"
	fi





	done 

	#Clear the capture pipe
    	>$capturePipe
 	echo "*----------------------------------------------------------------------*"
 	echo "$(tput setaf 6)Updated contact List:"
 	contlist=$(echo "l contact"|ionadmin|grep -o -P '(?<=from).*?(?=is)')
 	for li in "${contlist[@]}"; do
    	echo "$li"
	done
    	echo "$(tput setaf 7)Sleep for $updateInterval sec..."
	echo
	sleep $updateInterval

done


# Disable the trap on a normal exit.
trap - EXIT


