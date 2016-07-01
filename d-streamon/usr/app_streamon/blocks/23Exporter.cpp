	/**
	 * <blockinfo type="Info_Block" is_active="False" thread_exclusive="False">
	 *   <humandesc>
	 *   Receives a Packet message and prints its associated information (as returned by the methods in the Packet class)
	 *   </humandesc>
	 *
	 *   <shortdesc>Prints meta-information regarding a packet</shortdesc>
	 *
	 *   <gates>
	 *     <gate type="input" name="in_pkt" msg_type="Packet" m_start="0" m_end="0" />
	 *   </gates>
	 *
	 *   <paramsschema>
	 *    element params {
	 *      }
	 *   </paramsschema>
	 *
	 *   <paramsexample>
	 *     <params>
	 *     </params>
	 *   </paramsexample>
	 *
	 *   <variables>
	 *   </variables>
	 *
	 * </blockinfo>
	 */

	#include <Block.hpp>
	#include <BlockFactory.hpp>
	#include <Packet.hpp>
	#include <ClassId.hpp>
	#include "PacketPlus.hpp"
	#include <streamon/pptags.h>
	#include <external/zhelpers.h>
	#include <zmq.h>
	#include <iostream>
	#include <string.h>
	#include <errno.h>

	namespace bm
	{
	    class Exporter : public Block
	    {
	    	public:
	        Exporter(const std::string &name, bool) : Block(name, false),
	                                                  m_ingate_id(register_input_gate("in_gate")),
	                                                  m_outgate_id(register_output_gate("out_gate")),
	                                                  context(1),
	                                                  publisher(context, ZMQ_PUB)
	        {
	        }

	        Exporter(const Exporter &) = delete;            // you cannot copy block
	        Exporter& operator=(const Exporter &) = delete; // you cannot assign const block
	        Exporter(Exporter &&)=delete;                   // you cannot move block (c++11)
	        Exporter& operator=(Exporter &&) = delete;      // you cannot assign block


	        virtual ~Exporter()
	        {}
	        
	        /* The configure method allow to pass XML settings to the block
	         * using pugixml library
	         *
	         * Here you can place initialization of needed structures, before starting
	         * receive messages: for example you can put ZeroMQ configuration here.
	         */
	        virtual void _configure(const xml_node&  n)
	        {
				/*	ZeroMQ Configuration  
				 *
				 *	Context and publisher
				 *
				 */

			    std::string protocol = n.attribute("protocol").value();
			    std::string ip = n.attribute("ip").value();
			    std::string port = n.attribute("port").value();

			    std::string connection = protocol + "://"+ ip + ":" + port;
				
			    publisher.bind(connection.c_str());
			}
	        
	        /*
	         * Receive message method
	         * This method receives messages from previous block in chain (from ingate)
	         * 
	         * Here you should place your implementation: in the case of the exporter block
	         * this will receive all packets marked as EXPORTABLE by XFSM.
	         *
	         * Then simply sends to all subscribers the received packets.
	         *
	         */
	        virtual void _receive_msg(std::shared_ptr<const Msg>&& m, int /*id_ingate*/)
	        {
	            auto CurrentPacket = std::static_pointer_cast<const PacketPlus>(m);

	            auto Tags = CurrentPacket->getTags();

	            auto publishMap = Tags->zmq_publish;

	          	for (std::map<std::string,std::string>::iterator it=publishMap.begin(); it!=publishMap.end(); ++it)
	          	{
	    			//std::cout << it->first << " => " << it->second << '\n';
	    			
	    			s_sendmore (publisher, it->first.c_str());
        			
        			if(it->first == "raw")
	    			{
	    				std::string packet( (const char*)CurrentPacket->base(), CurrentPacket->length());
	    				s_send (publisher, packet);
	    			}
	    			else
	    			{
					s_send (publisher, it->second);
	    			}
	    		}
	        }
	        
	    private:
	        int m_ingate_id;
	        int m_outgate_id;

		zmq::context_t context;
	    	zmq::socket_t publisher;

	    };
	    
	    REGISTER_BLOCK(Exporter,"Exporter");
	}
