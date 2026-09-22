import { Flex, Text, Input, Field } from '@chakra-ui/react';
import { colors } from '../../../constants/colors';

const PreviewInput = ({
  title = '',
  value = '',
  placeholder = '',
  width = 300,
  maxLength,
  isDisabled = false,
  onChange
}) => {
  const handleChange = e => onChange?.(e.target.value);

  return (
    <Flex gap="4" align="center" mt="4">
      <Text fontSize="14px">{title}</Text>
      <Field.Root width="auto">
        <Input
          borderRadius="10px"
          bg={colors.bgBody}
          border={`1px solid ${colors.borderSecondary}`}
          h={9}
          w={`${width}px`}
          maxLength={maxLength}
          value={value}
          onChange={handleChange}
          placeholder={placeholder}
          disabled={isDisabled}
        />
      </Field.Root>
    </Flex>
  );
};

export default PreviewInput;
