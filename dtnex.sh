#!/bin/bash
# DTNEX scrip
# Network Information Exchange Mechanism for exchanging DTN Contacts
# Authors: Samo Grasic, samo@grasic.net

#Update time in seconds
updateInterval=10
contactLifetime=60
needToUpdate=false
bundleTTL=3600

if [ -f "nodemeta.conf" ]; then
	nodemetadata=$(cat nodemeta.conf)
fi


#Use this definition if you want to visualize the contact graph plan (Note:graphviz tool needs to be installed on the system)
createGraph=true
graphFile=/var/www/openipn.latelab.se/GetIPN/public/contactGraph.png

echo "Starting a DTNEX script, author: Samo Grasic (samo@grasic.net), v0.5 ..."

###Default settings
serviceNr=12160 #Do not change
msgidentifier="xmsg"
capturePipe=receivedmsgpipe

### Init commands
function init {
rm $capturePipe
touch $capturePipe
chmod 644 $capturePipe

ionOutput=$(echo "v"|bpadmin)
IFS=' ' read -ra versionline <<< $ionOutput
echo "Using ION Version:$(tput setaf 3)${versionline[1]}$(tput setaf 7)"
#Getting locally registered  endpoints
bpadminOutput=$(echo "l endpoint"|bpadmin)

#echo "*----------------------------------------------------------------------*"
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

}

###End of INIT function

### Function that gets the list of plans/neigboors
function getplanlist {
    echo "Getting a plan list (neighbour  nodes)..."
	plans=($(echo "l plan"|ipnadmin|sed 's@^[^0-9]*\([0-9]\+\).*@\1@'))
	unset plans[-1] # removes the last 1 elements
	unset plans[-1] # removes the last 1 elements
	unset plans[-1] # removes the last 1 elements
	echo "Number of configured plans:${#plans[@]}"
	echo "$(tput setaf 5)List of configured plans:"
	for i in "${plans[@]}"; do
  	echo ">$i"
	done



}

function getplanlist2 {
	echo "Getting a plan list (neighbour  nodes)..."
	plans=($(echo "l plan"|ipnadmin|sed 's@^[^0-9]*\([0-9]\+\).*@\1@'))
	unset plans[-1] # removes the last 1 elements
	unset plans[-1] # removes the last 1 elements
	unset plans[-1] # removes the last 1 elements
	echo "Number of configured plans:${#plans[@]}"
	echo "$(tput setaf 5)List of configured plans:"
	for i in "${plans[@]}"; do
		echo ">$i"
	done

	#Check if the plan list is updated
	needToUpdate=false
	if [ -f "planlist.txt" ]; then
			echo "Plan list file exists, reading the previous plan list..."
			readarray -t prevplans < planlist.txt
			prevTimestamp=${prevplans[0]}
			unset prevplans[0]
			currentTimestamp=$(date -u +%s)
			timeDiff=$((currentTimestamp - prevTimestamp))
			if [ "$timeDiff" -gt "$contactLifetime" ]; then
				#echo "Plan list has expired, updating the plan list file..."
				needToUpdate=true
			else
				if [ "${#prevplans[@]}" -ne "${#plans[@]}" ]; then
					#echo "Plan list is updated, updating the plan list file..."
					needToUpdate=true
				fi
			fi
		else
			#echo "Plan list file does not exist, creating the plan list file..."
			needToUpdate=true
		fi
	if [ "$needToUpdate" = true ]; then
		echo "Updating the plan list file..."
		echo "$(date -u +%s)" > planlist.txt
		for i in "${plans[@]}"; do
				echo "$i" >> planlist.txt
		done
	fi
}





### Function that exchage contacts with own list of plans
function exchagnewithneigboors2 {
	echo "Exchanging messages with configured plans:"
	for i in "${plans[@]}"; do
		plan=${i:0}
		if [[ "$nodeId" == "$plan" ]]; then
			echo "Skipping local loopback plan"
		else
			currentTimestamp=$(date -u +%s)
			#expireTime=$((currentTimestamp + contactLifetime)) #Absulute time not yet implemnted
			#expireTime=$((+$contactLifetime)) #Relative time
			echo "$(tput setaf 3)Messaging own plan to node [Origin:$nodeId, From:$nodeId, To:$plan, About:$nodeId, ExpireTime:+$contactLifetime]$(tput setaf 7)"
			bpsourceCommand="bpsource ipn:$plan.$serviceNr \"$msgidentifier 1 c $nodeId $nodeId $nodeId $plan +$contactLifetime \" -t$bundleTTL"
			echo $bpsourceCommand
			eval $bpsourceCommand
		fi
	done
	if [ -n "$nodemetadata" ]; then
		echo "Exchanging metadata with configured plans:"
		for i in "${plans[@]}"; do
			plan=${i:0}
			if [[ "$nodeId" == "$plan" ]]; then
				echo "Skipping local loopback plan"
			else
				echo "$(tput setaf 3)Messaging metadata to node [Origin:$nodeId, To Node:$$plan, Metadata:$nodemetadata]$(tput setaf 7)"
				bpsourceCommand="bpsource ipn:$plan.$serviceNr \"$msgidentifier 1 m $nodeId $nodeId $nodemetadata \" -t$bundleTTL"
				eval $bpsourceCommand
			fi
		done
	fi
}

function checkLine {
	if [[ $1 == *";"* || $1 == *"("* || $1 == *")"* || $1 == *"{"* || $1 == *"}"* || $1 == *"["* || $1 == *"]"* || $1 == *"|"* || $1 == *"&&"* || $1 == *"||"* ]]; then
		echo "Potential malicious message detected, skipping message:$line"
		if [[ -n "$1" ]]; then
			line=""
		fi
	fi
}

# I need to get a hash value of the string in bash, write a function that calculates it using basic bash commands using sha256
function gethash {
	echo -n $1 | sha256sum | awk '{print $1}'
}




#Processing received network messages
function processreceievedcontacts {
	echo "Processing received contacts..."
	cat $capturePipe
	cat $capturePipe|while read -r line
	do
			checkLine "$line"

	if [[ -n "$line" ]]; then
		echo "$(tput setaf 5)Received line:$line"
	fi

	if grep -q $msgidentifier <<<$line; then
	    	#Routing message received, processing received command
		echo "$(tput setaf 5)Contact message received, processing..."
		cmdarray=($line)
		echo "message array: ${cmdarray[@]}"
		echo "Number of elements in the messagearray: ${#cmdarray[@]}"
		if [[ "${cmdarray[1]}" == "1" ]];then
			#echo "Version 1 message received, parsing command..."
			if [[ "${cmdarray[2]}" == "c" ]];then
				msgOrigin=${cmdarray[3]}
				msgSentFrom=${cmdarray[4]}
				nodeA=${cmdarray[5]}
				nodeB=${cmdarray[6]}
				expireTime=${cmdarray[7]}
				echo "$(tput setaf 2)Contact received[Origin:$msgOrigin,From:$msgSentFrom,NodeA:$nodeA,NodeB:$nodeB, ExipreTime:$expireTime], updation ION...$(tput setaf 7)"
                		ionadminCommandContact1="echo \"a contact +1 $expireTime $nodeA $nodeB 100000\"|ionadmin"
                		echo $ionadminCommandContact1
				eval  $ionadminCommandContact1 >/dev/null
				ionadminCommandContact2="echo \"a contact +1 $expireTime $nodeB $nodeA 100000\"|ionadmin"
                                echo $ionadminCommandContact2
                                eval $ionadminCommandContact2>/dev/null
				ionadminCommandRange1="echo \"a range +1 $expireTime $nodeA $nodeB 1\"|ionadmin"
                                echo $ionadminCommandRange1
                                eval $ionadminCommandRange1>/dev/null
                                ionadminCommandRange2="echo \"a range +1 $expireTime $nodeB $nodeA 1\"|ionadmin"
                                echo $ionadminCommandRange2
                                eval $ionadminCommandRange2>/dev/null
				#echo "We forward information to all neigboors, except to the node that send or create link message..."
			        for out in "${plans[@]}"; do
        				outd=${out:0}
        				if [ "$msgOrigin" == "$outd" ] || [ "$msgSentFrom" == "$outd" ]  || [ "$nodeId" == "$outd" ] ; then
                				#echo "Skipping sending message to ourself or to the msg source node:$msgOrigin that sent us this message..."
        					a=0
					else
                				echo "$(tput setaf 5)Forwarding message[Origin:$msgOrigin,From:$msgSentFrom,To:$outd,NodeA:$nodeA,NodeB:$nodeB,ExpireTime:$expireTime]$(tput setaf 7)"
                				bpsourceForwardCommand="bpsource ipn:$out.$serviceNr \"$msgidentifier 1 c $msgOrigin $nodeId $nodeA $nodeB $expireTime \" -t$bundleTTL"
                				#echo $bpsourceForwardCommand
                				eval $bpsourceForwardCommand>/dev/null
        				fi
        			done

			else
			if [[ "${cmdarray[2]}" == "m" ]];then
				msgOrigin=${cmdarray[3]}
				msgSentFrom=${cmdarray[4]}
				#The metadata is the rest of the array elements expect the last one
				metadatareceived="${cmdarray[@]:5:${#cmdarray[@]}-1}"
				#Trim the metadata end of string from the last character
				metadatareceived=${metadatareceived::-1}
				# Remove the line that assigns the value to the unused variable
				echo "$(tput setaf 2)Node Metadata received[Origin:$msgOrigin,From:$msgSentFrom,Node Metadata:$metadatareceived], updating local metadata list...$(tput setaf 7)"

				#Update the local nodesmetadata.txt file with the received metadata. The file format is: "nodeId:metadata". If the node is not in the file, add it, if it is, update the metadata.
				if [ -f "nodesmetadata.txt" ]; then
					echo "Nodes metadata file exists, reading the previous metadata list..."
					readarray -t prevmetadata < nodesmetadata.txt
					#Check if the node is in the list
					nodefound=false
					for i in "${prevmetadata[@]}"; do
						if [[ $i == *"$msgOrigin"* ]]; then
							nodefound=true
							echo "Node found in the metadata list, updating the metadata..."
							#Update the metadata
							sed -i "s/$msgOrigin:.*/$msgOrigin:$metadatareceived/" nodesmetadata.txt
						fi
					done
					if [ "$nodefound" = false ]; then
						echo "Node not found in the metadata list, adding the node..."
						echo "$msgOrigin:$metadatareceived" >> nodesmetadata.txt
					fi
				else
					echo "Nodes metadata file does not exist, creating the metadata list file..."
					echo "$msgOrigin:$metadatareceived" >> nodesmetadata.txt
				fi

                #echo "We forward information to all neigboors, except to the node that send or create link message..."
			        for out in "${plans[@]}"; do
        				outd=${out:0}
        				if [ "$msgOrigin" == "$outd" ] || [ "$msgSentFrom" == "$outd" ]  || [ "$nodeId" == "$outd" ] ; then
                				#echo "Skipping sending message to ourself or to the msg source node:$msgOrigin that sent us this message..."
        					a=0
					else
                				echo "$(tput setaf 5)Forwarding Metadata[Origin:$msgOrigin,From:$msgSentFrom,To:$outd,Metadata:$metadatareceived]$(tput setaf 7)"
                				bpsourceForwardCommand="bpsource ipn:$out.$serviceNr \"$msgidentifier 1 m $msgOrigin $nodeId $metadatareceived \" -t$bundleTTL"
                				#echo $bpsourceForwardCommand
                				eval $bpsourceForwardCommand>/dev/null
        				fi
        			done
				else
			       	echo "Unknown message received!"
				fi
			fi
		else
			echo "Unknown version of message received!"
		fi
	#else
	#	echo "Unknown message received!"
	fi





	done 

	#Clear the capture pipe
    	>$capturePipe

}

#Functions calls ipnadmin to get the list of contacts. The output that is parsed is in the form of: "From  2024/06/19-21:52:40 to  2035/11/16-13:52:39 the xmit rate from node 268484800 to node 268484801 is     100000 bytes/sec, confidence 1.000000, regions 1 and 0." The function parses every line and creates three list that include numbers from first node, second node and the time difference between two dates in seconds.
function getcontacts {
	contstring=$(echo "l contact"|ionadmin|grep -o -P '(?<=From).*?(?=is)')
	IFS=$'\n' read -d '' -r -a contline <<< "$contstring"
#	if [ -n "${contline[*]}" ]; then
#	  for line in "${contline[@]}"; do
#		echo ">$line"
#	  done
#	fi
	echo "$(tput setaf 6)Number of contacts: $(echo "$contline" | wc -l)"
	#Loop through the list of contacts strings
	for contline in "${contline[@]}"; do
		if [ -n "$contline" ]; then
		#Print the contact line
		#echo "Contact line:$contline"
		#split line into words
		read -ra words <<< "$contline"
		#Loop through the words
		#echo "Words in the line:${#words[@]}"
		#for word in "${words[@]}"; do
		#	echo "Word:$word"
		#	done
		node1=${words[8]}
		node2=${words[11]}
			#Get dates and timediff
			IFS='-' read -ra date1 <<<"${words[0]}"
			IFS='-' read -ra date2 <<<"${words[2]}"
			#print date1
			#echo "Date1:${date1[0]} ${date1[1]}"
			#echo "Date2:${date2[0]} ${date2[1]}"
			#Then we parse the two dates
			IFS='/' read -ra date1d <<<"${date1[0]}"
			IFS=':' read -ra date1t <<<"${date1[1]}"
			IFS='/' read -ra date2d <<<"${date2[0]}"
			IFS=':' read -ra date2t <<<"${date2[1]}"
			#Then we calculate the time difference in seconds
			date1epoch=$(date -u -d "${date1d[0]}-${date1d[1]}-${date1d[2]} ${date1t[0]}:${date1t[1]}:${date1t[2]}" +%s)
			currentepoch=$(date -u +%s)
			date2epoch=$(date -u -d "${date2d[0]}-${date2d[1]}-${date2d[2]} ${date2t[0]}:${date2t[1]}:${date2t[2]}" +%s)
			timediff=$((date2epoch - currentepoch))
#			echo "$node1<--->$node2 ${date2[0]}-${date2[1]} ($timediff seconds left $date2epoch $date1epoch $currentepoch)"
			echo "$node1<--->$node2 ($timediff seconds left)"



		fi
	done
	
}

function creategraph {
	echo "Generating new graph visualization..."
	>contactGraph.gv
	echo "digraph G { layout=neato; overlap=false;">>contactGraph.gv
 	#Get the metadata list from file
	if [ -f "nodesmetadata.txt" ]; then
	#echo "Nodes metadata file exists, reading the metadata list..."
	readarray -t metadatalist < nodesmetadata.txt
	#Loop through the list of metadata strings
		for metadataline in "${metadatalist[@]}"; do
			#split line into words that are seperated by :
			IFS=':'
			read -ra metadatawords <<< "$metadataline"
			#Loop through the words
			#echo "Words in the line:${#metadatawords[@]}"
			#for metadataword in "${metadatawords[@]}"; do
			#	echo "Metadata word:$metadataword"
			#	done
			node=${metadatawords[0]}
			metadata=${metadatawords[1]}
			#Fix the metadata line, Escaping Characters: @ is replaced with &#64; and . with &#46; to ensure they are properly interpreted.
			metadata=$(echo $metadata|sed 's/@/\&#64;/g')
			metadata=$(echo $metadata|sed 's/\./\&#46;/g')
			#echo "Node:$node Metadata:$metadata"
			#Add the node to the graph
			echo "\"ipn:$node\" [label=< <FONT POINT-SIZE=\"14\" FACE=\"Arial\" COLOR=\"darkred\"><B>ipn:$node</B></FONT><BR/><FONT POINT-SIZE=\"10\" FACE=\"Arial\" COLOR=\"blue\">$metadata</FONT>>];">>contactGraph.gv
		done
	fi
	#Using the same template we generate .gv line for ourself using the nodemetadata and nodeid variables
	nodemetadata_gv=$(echo $nodemetadata|sed 's/@/\&#64;/g')
	nodemetadata_gv=$(echo $nodemetadata_gv|sed 's/\./\&#46;/g')
	echo "\"ipn:$nodeId\" [label=< <FONT POINT-SIZE=\"14\" FACE=\"Arial\" COLOR=\"darkred\"><B>ipn:$nodeId</B></FONT><BR/><FONT POINT-SIZE=\"10\" FACE=\"Arial\" COLOR=\"blue\">$nodemetadata_gv</FONT>>];">>contactGraph.gv 
	echo "$(tput setaf 6)Updated Contact Graph List:"
	contstr=$(echo "l contact"|ionadmin|grep -o -P '(?<=From).*?(?=is)')
	IFS=$'\n' read -d '' -r -a contli <<< "$contstr"
	#Loop through the list of contacts strings
	for contli in "${contli[@]}"; do
		if [ -n "$contli" ]; then
		#split line into words using space as a delimiter
		IFS=' '
		read -ra nodearray <<< "$contli"
		echo "\"ipn:${nodearray[8]}\" -> \"ipn:${nodearray[11]}\"">>contactGraph.gv
	#	echo "\"ipn:${nodearray[8]}\" -> \"ipn:${nodearray[11]}\""
		fi
	done
	
	echo "labelloc=\"t\"; label=\"IPNSIG's DTN Network Graph, Updated:$TIMESTAMP\"}">>contactGraph.gv
	dot -Tpng contactGraph.gv -o $graphFile    
}

# Main part of the script is now running under an ION watchdog
WATCH_INTERVAL=15
TIMEOUT=2
while true; do
    # Run bpadmin command to check Ion service and targeted service condition
	echo "Waiting for the ION service to start..."
    response=$(timeout "$TIMEOUT" bpadmin <<< "l" 2>&1 | tr -d '\r\n')
    exit_code="$?"
    datetime="$(date +'%Y-%m-%d %H:%M:%S')"
    if [ "$exit_code" -eq 0 ] && [[ "$response" == *List\ what?:* ]]; then
        echo "$datetime - Ion service is running. Targeted service condition is met."
        echo "q" | bpadmin

	init
	# While bpsink is running...
	while kill -0 $pid 2> /dev/null; do
	    # Do stuff
		echo
		TIMESTAMP=`date +%Y-%m-%d_%H-%M-%S`
		echo "Main Loop - TimeStamp:$TIMESTAMP"
		echo "Getting a fresh list of neighboors..."
		getplanlist2 #Get list of neigboors
		if [ "$needToUpdate" = true ]; then
			exchagnewithneigboors2
		fi
		processreceievedcontacts
    	getcontacts
		if [ "$createGraph" = "true" ]; then
			creategraph
		fi
		echo "$(tput setaf 7)Sleeping for $updateInterval sec..."
		echo
		sleep $updateInterval
	done
	# Disable the trap on a normal exit.
	trap - EXIT
    fi
    sleep "$WATCH_INTERVAL"
done





