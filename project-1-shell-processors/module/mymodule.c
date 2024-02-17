#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LARA-SUDE");
MODULE_DESCRIPTION("A module that knows psvis command and can be used to visualize the process tree");

//char *name;
//int age;

int curr_pid;
void psvis_traverse(struct task_struct* parent);

module_param(curr_pid, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
MODULE_PARM_DESC(curr_pid, "pid of the parent process");

/*
 * module_param(foo, int, 0000)
 * The first param is the parameters name
 * The second param is its data type
 * The final argument is the permissions bits,
 * for exposing parameters in sysfs (if non-zero) at a later stage.
 */
// A function that runs when the module is first loaded
int simple_init(void) {
	printk(KERN_INFO "psvis: Initializing the psvis kernel module\n");
	struct task_struct *parent = pid_task(find_vpid(curr_pid), PIDTYPE_PID);
	if(parent==NULL)
	{
		printk(KERN_INFO "psvis: Parent process with pid %d not found\n", curr_pid);
		return -1;
	}
	psvis_traverse(parent);
	
	return 0; // Non-zero return means that the module couldn't be loaded.
	//struct task_struct *ts;
}

void psvis_traverse(struct task_struct *parent) //traverse the process tree 
{
	struct task_struct* child;//child process
	struct list_head* list_children;//list of children
	uint64_t start_timeP= parent->start_time;//start time of the process
	pid_t pid_parent= parent->curr_pid;//process id


	//Finding the oldest child to colour in a different colour
	struct task_struct* oldest_child = NULL; //oldest child process
	struct task_struct* child_f;
	pid_t oldest_child_pid = -1; //Stores the PID of the oldest child process.

	find_oldest_child(list_children, &parent->children) //to find the oldest child process based on the start_time attribute.
	{
		child_f = list_entry(list_children, struct task_struct, sibling);
		//printk(KERN_INFO "psvis: %d %s\n", child->pid, child->comm);
		uint64_t start_time_c = child_f->start_time;
	    if (start_time_c < start_timeP)
		{
			oldest_child = child_f;
			
			start_timeP = start_time_c;

			oldest_child_pid= oldest_child->curr_pid;
		} 	
		
	}
	show_process_tree(list_children, &parent->children){ //to show the process tree 
		child = list_entry(list_children, struct task_struct, sibling);
		u64 start_time_c = child->start_timeP;

		//pid_t pid_child = child->pid;
		if (child == oldest_child)
		{
			if (oldest_child_pid == child->curr_pid) //colorize the oldest child 
			{
				printk("\t\"pid=%d Starting time=%lld\"[fillcolor=green, style=filled];\n", child->curr_pid, start_time_c);
			}
		}

		printk("\t\"pid=%d Starting time=%lld\" -- \"pid=%d Starting time=%lld\";\n", pid_parent, start_timeP, child->curr_pid, start_time_c);//print the process tree
    	psvis_traverse(child);//recursive call to traverse the process tree
	}
}


// A function that runs when the module is removed
void simple_exit(void) {
	//printk("Goodbye from the kernel, user: %s, age: %d\n", name, age);
	printk(KERN_INFO "psvis: Exiting the psvis kernel module\n");
}

module_init(simple_init);//to initialize the module
module_exit(simple_exit);//to exit the module