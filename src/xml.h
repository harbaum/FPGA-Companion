#ifndef XML_H
#define XML_H

// callbacks
extern int xml_element_start_cb(char *name);
extern void xml_element_end_cb(void);
extern void xml_attribute_cb(char *name, char *value);

void xml_init(void);
int xml_parse(char);


#endif // XML_H
